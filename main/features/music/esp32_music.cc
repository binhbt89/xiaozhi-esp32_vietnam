#include "esp32_music.h"
#include "board.h"
#include "system_info.h"
#include "audio/audio_codec.h"
#include "application.h"
#include "protocols/protocol.h"
#include "display/display.h"
#include "settings.h"

#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_pthread.h>
#include <esp_timer.h>
#include <cJSON.h>
#include <cstring>
#include <chrono>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <thread>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "Esp32Music"

// Default ZingMP3 server URL - CHANGE THIS TO YOUR SERVER IP
#define DEFAULT_ZINGMP3_URL "http://192.168.1.100:5555"

// URL encoding function
static std::string url_encode(const std::string& str) {
    std::string encoded;
    char hex[4];
    
    for (size_t i = 0; i < str.length(); i++) {
        unsigned char c = str[i];
        
        if ((c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += c;
        } else if (c == ' ') {
            encoded += '+';
        } else {
            snprintf(hex, sizeof(hex), "%%%02X", c);
            encoded += hex;
        }
    }
    return encoded;
}

Esp32Music::Esp32Music() 
    : zingmp3_server_url_(DEFAULT_ZINGMP3_URL),
      last_downloaded_data_(), 
      current_music_url_(), 
      current_song_id_(),
      current_song_info_(),
      song_name_displayed_(false),
      full_info_displayed_(false),
      fft_started_(false),
      preferred_quality_(QUALITY_128),
      current_lyric_url_(), 
      lyrics_(), 
      current_lyric_index_(-1), 
      lyric_thread_(), 
      is_lyric_running_(false),
      display_mode_(DISPLAY_MODE_SPECTRUM), 
      is_playing_(false), 
      is_downloading_(false), 
      play_thread_(), 
      download_thread_(), 
      audio_buffer_(), 
      buffer_mutex_(), 
      buffer_cv_(), 
      buffer_size_(0), 
      mp3_decoder_(nullptr), 
      mp3_frame_info_(), 
      mp3_decoder_initialized_(false),
      current_play_time_ms_(0),
      last_frame_time_ms_(0),
      total_frames_decoded_(0) {
    
    // Load server URL from settings using GetCheckMusicServerUrl()
    // Note: GetCheckMusicServerUrl() will be called when needed, not in constructor
}

Esp32Music::~Esp32Music() {
    ESP_LOGI(TAG, "Destroying ZingMP3 player - stopping all operations");
    
    // Stop all operations
    is_downloading_ = false;
    is_playing_ = false;
    is_lyric_running_ = false;
    
    // Notify all waiting threads
    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        buffer_cv_.notify_all();
    }
    
    // Wait for download thread
    if (download_thread_.joinable()) {
        ESP_LOGI(TAG, "Waiting for download thread to finish");
        download_thread_.join();
    }
    
    // Wait for playback thread
    if (play_thread_.joinable()) {
        ESP_LOGI(TAG, "Waiting for playback thread to finish");
        play_thread_.join();
    }
    
    // Wait for lyric thread
    if (lyric_thread_.joinable()) {
        ESP_LOGI(TAG, "Waiting for lyric thread to finish");
        lyric_thread_.join();
    }
    
    // Clear buffer and decoder
    ClearAudioBuffer();
    CleanupMp3Decoder();
    
    ESP_LOGI(TAG, "ZingMP3 player destroyed successfully");
}

void Esp32Music::Initialize() {
    ESP_LOGI(TAG, "Initializing ZingMP3 player");
    // Get server URL from settings
    zingmp3_server_url_ = GetCheckMusicServerUrl();
    ESP_LOGI(TAG, "Server URL: %s", zingmp3_server_url_.c_str());
}

std::string Esp32Music::GetQualityString(AudioQuality quality) {
    switch (quality) {
        case QUALITY_128: return "128";
        case QUALITY_320: return "320";
        case QUALITY_LOSSLESS: return "lossless";
        default: return "128";
    }
}

bool Esp32Music::SearchSong(const std::string& query, SongInfo& song_info) {
    ESP_LOGI(TAG, "Searching for: %s", query.c_str());
    
    std::string search_url = zingmp3_server_url_ + "/api/search?q=" + url_encode(query);
    ESP_LOGI(TAG, "Search URL: %s", search_url.c_str());
    
    auto network = Board::GetInstance().GetNetwork();
    auto http = network->CreateHttp(0);
    
    http->SetHeader("User-Agent", "ESP32-ZingMP3-Player/1.0");
    http->SetHeader("Accept", "application/json");
    
    if (!http->Open("GET", search_url)) {
        ESP_LOGE(TAG, "Failed to connect to ZingMP3 search API");
        return false;
    }
    
    int status_code = http->GetStatusCode();
    if (status_code != 200) {
        ESP_LOGE(TAG, "Search API failed with status: %d", status_code);
        http->Close();
        return false;
    }
    
    std::string response = http->ReadAll();
    http->Close();
    
    ESP_LOGD(TAG, "Search response: %s", response.c_str());
    
    // Parse JSON response
    cJSON* json = cJSON_Parse(response.c_str());
    if (!json) {
        ESP_LOGE(TAG, "Failed to parse search response JSON");
        return false;
    }
    
    bool found = false;
    cJSON* data = cJSON_GetObjectItem(json, "data");
    if (data) {
        // Try to get top result first
        cJSON* top = cJSON_GetObjectItem(data, "top");
        if (top && cJSON_IsObject(top)) {
            cJSON* id = cJSON_GetObjectItem(top, "encodeId");
            cJSON* title = cJSON_GetObjectItem(top, "title");
            cJSON* artist = cJSON_GetObjectItem(top, "artistsNames");
            
            if (cJSON_IsString(id) && id->valuestring) {
                song_info.encodeId = id->valuestring;
                if (cJSON_IsString(title)) song_info.title = title->valuestring;
                if (cJSON_IsString(artist)) song_info.artist = artist->valuestring;
                found = true;
                ESP_LOGI(TAG, "Found top result: %s - %s (ID: %s)", 
                         song_info.title.c_str(), song_info.artist.c_str(), song_info.encodeId.c_str());
            }
        }
        
        // If no top result, try songs array
        if (!found) {
            cJSON* songs = cJSON_GetObjectItem(data, "songs");
            if (cJSON_IsArray(songs) && cJSON_GetArraySize(songs) > 0) {
                cJSON* first_song = cJSON_GetArrayItem(songs, 0);
                if (first_song) {
                    cJSON* id = cJSON_GetObjectItem(first_song, "encodeId");
                    cJSON* title = cJSON_GetObjectItem(first_song, "title");
                    cJSON* artist = cJSON_GetObjectItem(first_song, "artistsNames");
                    
                    if (cJSON_IsString(id) && id->valuestring) {
                        song_info.encodeId = id->valuestring;
                        if (cJSON_IsString(title)) song_info.title = title->valuestring;
                        if (cJSON_IsString(artist)) song_info.artist = artist->valuestring;
                        found = true;
                        ESP_LOGI(TAG, "Found in songs: %s - %s (ID: %s)", 
                                 song_info.title.c_str(), song_info.artist.c_str(), song_info.encodeId.c_str());
                    }
                }
            }
        }
    }
    
    cJSON_Delete(json);
    
    if (!found) {
        ESP_LOGE(TAG, "No songs found for query: %s", query.c_str());
    }
    
    return found;
}

bool Esp32Music::GetSongInfo(const std::string& song_id, SongInfo& song_info) {
    ESP_LOGI(TAG, "Getting song info for ID: %s", song_id.c_str());
    
    std::string info_url = zingmp3_server_url_ + "/api/info-song?id=" + song_id;
    
    auto network = Board::GetInstance().GetNetwork();
    auto http = network->CreateHttp(0);
    
    http->SetHeader("User-Agent", "ESP32-ZingMP3-Player/1.0");
    http->SetHeader("Accept", "application/json");
    
    if (!http->Open("GET", info_url)) {
        ESP_LOGE(TAG, "Failed to connect to song info API");
        return false;
    }
    
    int status_code = http->GetStatusCode();
    if (status_code != 200) {
        ESP_LOGE(TAG, "Song info API failed with status: %d", status_code);
        http->Close();
        return false;
    }
    
    std::string response = http->ReadAll();
    http->Close();
    
    // Parse JSON
    cJSON* json = cJSON_Parse(response.c_str());
    if (!json) {
        ESP_LOGE(TAG, "Failed to parse song info JSON");
        return false;
    }
    
    bool success = false;
    cJSON* data = cJSON_GetObjectItem(json, "data");
    if (data) {
        cJSON* title = cJSON_GetObjectItem(data, "title");
        cJSON* artist = cJSON_GetObjectItem(data, "artistsNames");
        cJSON* duration = cJSON_GetObjectItem(data, "duration");
        cJSON* thumbnail = cJSON_GetObjectItem(data, "thumbnailM");
        
        song_info.encodeId = song_id;
        if (cJSON_IsString(title)) song_info.title = title->valuestring;
        if (cJSON_IsString(artist)) song_info.artist = artist->valuestring;
        if (cJSON_IsNumber(duration)) song_info.duration = duration->valueint;
        if (cJSON_IsString(thumbnail)) song_info.thumbnailUrl = thumbnail->valuestring;
        
        ESP_LOGI(TAG, "Song info: %s - %s (%ds)", 
                 song_info.title.c_str(), song_info.artist.c_str(), song_info.duration);
        success = true;
    }
    
    cJSON_Delete(json);
    return success;
}

bool Esp32Music::GetStreamUrl(const std::string& song_id, AudioQuality quality, std::string& stream_url) {
    ESP_LOGI(TAG, "Getting stream URL for ID: %s, quality: %s", 
             song_id.c_str(), GetQualityString(quality).c_str());
    
    std::string api_url = zingmp3_server_url_ + "/api/song?id=" + song_id;
    
    auto network = Board::GetInstance().GetNetwork();
    auto http = network->CreateHttp(0);
    
    http->SetHeader("User-Agent", "ESP32-ZingMP3-Player/1.0");
    http->SetHeader("Accept", "application/json");
    
    if (!http->Open("GET", api_url)) {
        ESP_LOGE(TAG, "Failed to connect to song API");
        return false;
    }
    
    int status_code = http->GetStatusCode();
    if (status_code != 200) {
        ESP_LOGE(TAG, "Song API failed with status: %d", status_code);
        http->Close();
        return false;
    }
    
    std::string response = http->ReadAll();
    http->Close();
    
    // Parse JSON
    cJSON* json = cJSON_Parse(response.c_str());
    if (!json) {
        ESP_LOGE(TAG, "Failed to parse song stream JSON");
        return false;
    }
    
    bool success = false;
    cJSON* data = cJSON_GetObjectItem(json, "data");
    if (data) {
        std::string quality_key = GetQualityString(quality);
        cJSON* url_item = cJSON_GetObjectItem(data, quality_key.c_str());
        
        if (cJSON_IsString(url_item) && url_item->valuestring && strlen(url_item->valuestring) > 0) {
            stream_url = url_item->valuestring;
            ESP_LOGI(TAG, "Got stream URL for quality %s", quality_key.c_str());
            success = true;
        } else {
            // Try fallback to 128kbps if requested quality not available
            if (quality != QUALITY_128) {
                ESP_LOGW(TAG, "Quality %s not available, trying 128kbps", quality_key.c_str());
                url_item = cJSON_GetObjectItem(data, "128");
                if (cJSON_IsString(url_item) && url_item->valuestring && strlen(url_item->valuestring) > 0) {
                    stream_url = url_item->valuestring;
                    ESP_LOGI(TAG, "Using 128kbps fallback");
                    success = true;
                }
            }
        }
    }
    
    cJSON_Delete(json);
    
    if (!success) {
        ESP_LOGE(TAG, "No stream URL found for song ID: %s", song_id.c_str());
    }
    
    return success;
}

bool Esp32Music::Download(const std::string& song_name, const std::string& artist_name) {
    ESP_LOGI(TAG, "Download request: %s - %s", song_name.c_str(), artist_name.c_str());
    
    // Clear previous state
    last_downloaded_data_.clear();
    current_song_id_.clear();
    current_song_info_ = SongInfo();
    
    // Step 1: Search for the song
    std::string search_query = song_name;
    if (!artist_name.empty()) {
        search_query += " " + artist_name;
    }
    
    SongInfo search_result;
    if (!SearchSong(search_query, search_result)) {
        ESP_LOGE(TAG, "Failed to find song: %s", search_query.c_str());
        return false;
    }
    
    current_song_id_ = search_result.encodeId;
    
    // Step 2: Get detailed song info
    if (!GetSongInfo(current_song_id_, current_song_info_)) {
        ESP_LOGW(TAG, "Failed to get detailed song info, using search result");
        current_song_info_ = search_result;
    }
    
    // Step 3: Get stream URL
    std::string stream_url;
    if (!GetStreamUrl(current_song_id_, preferred_quality_, stream_url)) {
        ESP_LOGE(TAG, "Failed to get stream URL");
        return false;
    }
    
    current_music_url_ = stream_url;
    
    // Display song info early
    if (display_mode_ == DISPLAY_MODE_SPECTRUM) {
        auto display = Board::GetInstance().GetDisplay();
        if (display) {
            char buf[256];
            snprintf(buf, sizeof(buf),
                     "ZINGMP3 《%s》\n%s • Đang phát...",
                     current_song_info_.title.c_str(),
                     current_song_info_.artist.c_str());
            display->SetMusicInfo(buf);
            ESP_LOGI(TAG, "Early display: %s", buf);
        }
    }
    
    // Step 4: Start streaming
    ESP_LOGI(TAG, "Starting playback: %s - %s", 
             current_song_info_.title.c_str(), current_song_info_.artist.c_str());
    song_name_displayed_ = false;
    full_info_displayed_ = false;
    
    StartStreaming(current_music_url_);
    
    // Step 5: Get lyrics if in lyric mode
    if (display_mode_ == DISPLAY_MODE_LYRICS) {
        ESP_LOGI(TAG, "Loading lyrics for song ID: %s", current_song_id_.c_str());
        
        if (is_lyric_running_) {
            is_lyric_running_ = false;
            if (lyric_thread_.joinable()) {
                lyric_thread_.join();
            }
        }
        
        is_lyric_running_ = true;
        current_lyric_index_ = -1;
        lyrics_.clear();
        
        lyric_thread_ = std::thread(&Esp32Music::LyricDisplayThread, this);
    }
    
    return true;
}

std::string Esp32Music::GetDownloadResult() {
    return last_downloaded_data_;
}

bool Esp32Music::StartStreaming(const std::string& music_url) {
    if (music_url.empty()) {
        ESP_LOGE(TAG, "Music URL is empty");
        return false;
    }
    
    ESP_LOGI(TAG, "Starting streaming: %s", music_url.c_str());
    
    // Stop previous playback
    is_downloading_ = false;
    is_playing_ = false;
    
    // Clear FFT and reset flags
    {
        auto display = Board::GetInstance().GetDisplay();
        if (display) {
            display->StopFFT();
            display->ReleaseAudioBuffFFT();
        }
    }
    fft_started_ = false;
    full_info_displayed_ = false;
    song_name_displayed_ = false;
    
    // Wait for previous threads
    if (download_thread_.joinable()) {
        {
            std::lock_guard<std::mutex> lock(buffer_mutex_);
            buffer_cv_.notify_all();
        }
        download_thread_.join();
    }
    if (play_thread_.joinable()) {
        {
            std::lock_guard<std::mutex> lock(buffer_mutex_);
            buffer_cv_.notify_all();
        }
        play_thread_.join();
    }
    
    // Clear buffer
    ClearAudioBuffer();
    
    // Configure thread
    esp_pthread_cfg_t cfg = esp_pthread_get_default_config();
    cfg.stack_size = 1024 * 3;
    cfg.prio = 5;
    cfg.thread_name = "zingmp3_stream";
    esp_pthread_set_cfg(&cfg);
    
    // Start threads
    is_downloading_ = true;
    download_thread_ = std::thread(&Esp32Music::DownloadAudioStream, this, music_url);
    
    is_playing_ = true;
    play_thread_ = std::thread(&Esp32Music::PlayAudioStream, this);
    
    ESP_LOGI(TAG, "Streaming threads started");
    return true;
}

bool Esp32Music::StopStreaming() {
    if (!is_playing_ && !is_downloading_) {
        ESP_LOGW(TAG, "No streaming in progress");
        return true;
    }
    
    ESP_LOGI(TAG, "Stopping streaming");
    
    ResetSampleRate();
    
    is_downloading_ = false;
    is_playing_ = false;
    
    // Clear display
    auto display = Board::GetInstance().GetDisplay();
    if (display) {
        display->SetMusicInfo("");
    }
    
    // Notify threads
    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        buffer_cv_.notify_all();
    }
    
    // Wait for threads
    if (download_thread_.joinable()) {
        download_thread_.join();
    }
    if (play_thread_.joinable()) {
        play_thread_.join();
    }
    
    // Stop FFT
    if (display && display_mode_ == DISPLAY_MODE_SPECTRUM) {
        display->StopFFT();
        display->ReleaseAudioBuffFFT();
        final_pcm_data_fft = nullptr;
    }
    
    fft_started_ = false;
    current_song_id_.clear();
    current_song_info_ = SongInfo();
    
    ESP_LOGI(TAG, "Streaming stopped");
    return true;
}

// Reuse the same streaming implementation from esp32_music.cc
void Esp32Music::DownloadAudioStream(const std::string& music_url) {
    ESP_LOGI(TAG, "Starting audio download from: %s", music_url.c_str());
    
    if (music_url.empty() || music_url.find("http") != 0) {
        ESP_LOGE(TAG, "Invalid URL: %s", music_url.c_str());
        is_downloading_ = false;
        return;
    }
    
    auto network = Board::GetInstance().GetNetwork();
    auto http = network->CreateHttp(0);
    
    http->SetHeader("User-Agent", "ESP32-ZingMP3-Player/1.0");
    http->SetHeader("Accept", "*/*");
    http->SetHeader("Range", "bytes=0-");
    http->SetHeader("Connection", "keep-alive");
    http->SetHeader("Cache-Control", "no-cache");
    
    if (!http->Open("GET", music_url)) {
        ESP_LOGE(TAG, "Failed to connect to stream URL");
        is_downloading_ = false;
        return;
    }
    
    int status_code = http->GetStatusCode();
    if (status_code != 200 && status_code != 206) {
        ESP_LOGE(TAG, "HTTP failed with status: %d", status_code);
        http->Close();
        is_downloading_ = false;
        return;
    }
    
    ESP_LOGI(TAG, "Download started, status: %d", status_code);
    
    const size_t chunk_size = 4096;
    char* buffer = new char[chunk_size];
    size_t total_downloaded = 0;
    size_t total_print_bytes = 0;
    
    while (is_downloading_ && is_playing_) {
        int bytes_read = http->Read(buffer, chunk_size);
        if (bytes_read < 0) {
            ESP_LOGE(TAG, "Read error: %d", bytes_read);
            break;
        }
        if (bytes_read == 0) {
            ESP_LOGI(TAG, "Download complete: %d bytes", total_downloaded);
            break;
        }
        
        // Detect file format on first chunk
        if (total_downloaded == 0 && bytes_read >= 4) {
            if (memcmp(buffer, "ID3", 3) == 0) {
                ESP_LOGI(TAG, "Detected MP3 with ID3 tag");
            } else if (buffer[0] == 0xFF && (buffer[1] & 0xE0) == 0xE0) {
                ESP_LOGI(TAG, "Detected MP3 header");
            }
        }
        
        // Allocate chunk
        uint8_t* chunk_data = (uint8_t*)heap_caps_malloc(bytes_read, MALLOC_CAP_SPIRAM);
        if (!chunk_data) {
            ESP_LOGE(TAG, "Failed to allocate chunk memory");
            break;
        }
        memcpy(chunk_data, buffer, bytes_read);
        
        // Add to buffer
        {
            std::unique_lock<std::mutex> lock(buffer_mutex_);
            buffer_cv_.wait(lock, [this] { return buffer_size_ < MAX_BUFFER_SIZE || !is_downloading_; });
            
            if (is_downloading_) {
                audio_buffer_.push(AudioChunk(chunk_data, bytes_read));
                buffer_size_ += bytes_read;
                total_downloaded += bytes_read;
                total_print_bytes += bytes_read;
                
                buffer_cv_.notify_one();
                
                if (total_print_bytes >= (128 * 1024)) {
                    total_print_bytes = 0;
                    ESP_LOGI(TAG, "Downloaded %d bytes, buffer: %d", total_downloaded, buffer_size_);
                }
            } else {
                heap_caps_free(chunk_data);
                break;
            }
        }
    }
    
    delete[] buffer;
    http->Close();
    
    ESP_LOGI(TAG, "Download thread finished, total: %d bytes", total_downloaded);
    is_downloading_ = false;
    
    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        buffer_cv_.notify_all();
    }
}

void Esp32Music::PlayAudioStream() {
    ESP_LOGI(TAG, "Starting playback thread");
    
    current_play_time_ms_ = 0;
    last_frame_time_ms_ = 0;
    total_frames_decoded_ = 0;
    
    auto codec = Board::GetInstance().GetAudioCodec();
    if (!codec) {
        ESP_LOGE(TAG, "Audio codec not available");
        is_playing_ = false;
        return;
    }
    
    if (!codec->output_enabled()) {
        codec->EnableOutput(true);
    }
    
    if (!mp3_decoder_initialized_) {
        InitializeMp3Decoder();
    }
    
    // Wait for buffer
    {
        std::unique_lock<std::mutex> lock(buffer_mutex_);
        buffer_cv_.wait(lock, [this] { 
            return buffer_size_ >= MIN_BUFFER_SIZE || (!is_downloading_ && !audio_buffer_.empty()); 
        });
    }
    
    ESP_LOGI(TAG, "Starting playback with buffer: %d", buffer_size_);
    
    size_t total_played_bytes = 0;
    uint8_t* mp3_input_buffer = nullptr;
    int bytes_left = 0;
    uint8_t* read_ptr = nullptr;
    
    constexpr int INPUT_BUF = 8192;
    mp3_input_buffer = (uint8_t*)heap_caps_malloc(INPUT_BUF, MALLOC_CAP_SPIRAM);
    if (!mp3_input_buffer) {
        ESP_LOGE(TAG, "Failed to allocate MP3 buffer");
        is_playing_ = false;
        return;
    }
    
    bool id3_processed = false;
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    int16_t* pcm_buffer = new int16_t[2304];
    if (!pcm_buffer) {
        ESP_LOGE(TAG, "Failed to allocate PCM buffer");
        heap_caps_free(mp3_input_buffer);
        is_playing_ = false;
        return;
    }
    
    while (is_playing_) {
        // Check device state
        auto& app = Application::GetInstance();
        DeviceState current_state = app.GetDeviceState();
        
        if (current_state == kDeviceStateListening || current_state == kDeviceStateSpeaking) {
            app.ToggleChatState();
            vTaskDelay(pdMS_TO_TICKS(300));
            continue;
        } else if (current_state != kDeviceStateIdle) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        
        // Start FFT once
        if (!fft_started_) {
            if (display && display_mode_ == DISPLAY_MODE_SPECTRUM) {
                vTaskDelay(pdMS_TO_TICKS(150));
                display->StartFFT();
                ESP_LOGI(TAG, "FFT started");
                
                // Refresh song info
                if (!current_song_info_.title.empty()) {
                    char buf[256];
                    snprintf(buf, sizeof(buf),
                             "ZINGMP3 《%s》\n%s • Đang phát...",
                             current_song_info_.title.c_str(),
                             current_song_info_.artist.c_str());
                    display->SetMusicInfo(buf);
                }
            }
            fft_started_ = true;
        }
        
        if (!song_name_displayed_ && !current_song_info_.title.empty()) {
            song_name_displayed_ = true;
        }
        
        // Read more data if needed
        if (bytes_left < (INPUT_BUF / 2)) {
            AudioChunk chunk;
            
            {
                std::unique_lock<std::mutex> lock(buffer_mutex_);
                if (audio_buffer_.empty()) {
                    if (!is_downloading_) {
                        ESP_LOGI(TAG, "Playback finished");
                        break;
                    }
                    buffer_cv_.wait(lock, [this] { return !audio_buffer_.empty() || !is_downloading_; });
                    if (audio_buffer_.empty()) {
                        continue;
                    }
                }
                
                chunk = audio_buffer_.front();
                audio_buffer_.pop();
                buffer_size_ -= chunk.size;
                total_played_bytes += chunk.size;
                
                buffer_cv_.notify_one();
            }
            
            if (chunk.data && chunk.size > 0) {
                if (bytes_left > 0 && read_ptr != mp3_input_buffer) {
                    memmove(mp3_input_buffer, read_ptr, bytes_left);
                }
                
                size_t space_available = INPUT_BUF - bytes_left;
                size_t copy_size = std::min(chunk.size, space_available);
                
                memcpy(mp3_input_buffer + bytes_left, chunk.data, copy_size);
                bytes_left += copy_size;
                read_ptr = mp3_input_buffer;
                memset(mp3_input_buffer + bytes_left, 0, INPUT_BUF - bytes_left);
                
                // Skip ID3 tag
                if (!id3_processed && bytes_left >= 10) {
                    size_t id3_skip = SkipId3Tag(read_ptr, bytes_left);
                    if (id3_skip > 0) {
                        read_ptr += id3_skip;
                        bytes_left -= id3_skip;
                        ESP_LOGI(TAG, "Skipped ID3: %u bytes", (unsigned int)id3_skip);
                        
                        if (bytes_left < 256) {
                            bytes_left = 0;
                            heap_caps_free(chunk.data);
                            continue;
                        }
                    }
                    
                    if (bytes_left >= 256) {
                        id3_processed = true;
                    }
                }
                
                heap_caps_free(chunk.data);
            }
        }
        
        // Find MP3 sync
        int sync_offset = MP3FindSyncWord(read_ptr, bytes_left);
        if (sync_offset < 0) {
            vTaskDelay(pdMS_TO_TICKS(2));
            bytes_left = 0;
            continue;
        }
        
        if (bytes_left < 128) {
            vTaskDelay(pdMS_TO_TICKS(2));
            continue;
        }
        
        if (sync_offset > 0) {
            read_ptr += sync_offset;
            bytes_left -= sync_offset;
        }
        
        // Decode MP3 frame
        int decode_result = MP3Decode(mp3_decoder_, &read_ptr, &bytes_left, pcm_buffer, 0);
        if (decode_result != 0) {
            vTaskDelay(pdMS_TO_TICKS(2));
            continue;
        }
        
        MP3GetLastFrameInfo(mp3_decoder_, &mp3_frame_info_);
        
        // Update playback time
        if (mp3_frame_info_.samprate > 0) {
            int frame_duration_ms = (mp3_frame_info_.outputSamps / mp3_frame_info_.nChans * 1000) / mp3_frame_info_.samprate;
            current_play_time_ms_ += frame_duration_ms;
            total_frames_decoded_++;
        }
        
        // Play audio
        size_t samples_to_write = mp3_frame_info_.outputSamps;
        final_pcm_data_fft = pcm_buffer;
        
        codec->Write((uint8_t*)pcm_buffer, samples_to_write * sizeof(int16_t));
    }
    
    delete[] pcm_buffer;
    heap_caps_free(mp3_input_buffer);
    
    ESP_LOGI(TAG, "Playback thread finished");
    is_playing_ = false;
}

void Esp32Music::ClearAudioBuffer() {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    while (!audio_buffer_.empty()) {
        auto chunk = audio_buffer_.front();
        if (chunk.data) {
            heap_caps_free(chunk.data);
        }
        audio_buffer_.pop();
    }
    buffer_size_ = 0;
}

bool Esp32Music::InitializeMp3Decoder() {
    if (mp3_decoder_initialized_) {
        return true;
    }
    
    mp3_decoder_ = MP3InitDecoder();
    if (!mp3_decoder_) {
        ESP_LOGE(TAG, "Failed to initialize MP3 decoder");
        return false;
    }
    
    mp3_decoder_initialized_ = true;
    ESP_LOGI(TAG, "MP3 decoder initialized");
    return true;
}

void Esp32Music::CleanupMp3Decoder() {
    if (mp3_decoder_initialized_ && mp3_decoder_) {
        MP3FreeDecoder(mp3_decoder_);
        mp3_decoder_ = nullptr;
        mp3_decoder_initialized_ = false;
        ESP_LOGI(TAG, "MP3 decoder cleaned up");
    }
}

void Esp32Music::ResetSampleRate() {
    auto codec = Board::GetInstance().GetAudioCodec();
    if (codec) {
        // Reset to default sample rate if needed
        ESP_LOGI(TAG, "Sample rate reset");
    }
}

size_t Esp32Music::SkipId3Tag(uint8_t* data, size_t size) {
    if (size < 10) return 0;
    
    if (data[0] == 'I' && data[1] == 'D' && data[2] == '3') {
        size_t tag_size = ((data[6] & 0x7F) << 21) |
                         ((data[7] & 0x7F) << 14) |
                         ((data[8] & 0x7F) << 7) |
                         (data[9] & 0x7F);
        return tag_size + 10;
    }
    
    return 0;
}

bool Esp32Music::GetLyrics(const std::string& song_id) {
    ESP_LOGI(TAG, "Getting lyrics for song ID: %s", song_id.c_str());
    
    std::string lyric_url = zingmp3_server_url_ + "/api/lyric?id=" + song_id;
    
    auto network = Board::GetInstance().GetNetwork();
    auto http = network->CreateHttp(0);
    
    http->SetHeader("User-Agent", "ESP32-ZingMP3-Player/1.0");
    http->SetHeader("Accept", "application/json");
    
    if (!http->Open("GET", lyric_url)) {
        ESP_LOGE(TAG, "Failed to connect to lyric API");
        return false;
    }
    
    int status_code = http->GetStatusCode();
    if (status_code != 200) {
        ESP_LOGE(TAG, "Lyric API failed with status: %d", status_code);
        http->Close();
        return false;
    }
    
    std::string response = http->ReadAll();
    http->Close();
    
    return ParseZingMp3Lyrics(response);
}

bool Esp32Music::ParseZingMp3Lyrics(const std::string& lyric_json) {
    cJSON* json = cJSON_Parse(lyric_json.c_str());
    if (!json) {
        ESP_LOGE(TAG, "Failed to parse lyric JSON");
        return false;
    }
    
    bool success = false;
    cJSON* data = cJSON_GetObjectItem(json, "data");
    if (data) {
        cJSON* sentences = cJSON_GetObjectItem(data, "sentences");
        if (cJSON_IsArray(sentences)) {
            std::lock_guard<std::mutex> lock(lyrics_mutex_);
            lyrics_.clear();
            
            int array_size = cJSON_GetArraySize(sentences);
            for (int i = 0; i < array_size; i++) {
                cJSON* sentence = cJSON_GetArrayItem(sentences, i);
                if (sentence) {
                    cJSON* words = cJSON_GetObjectItem(sentence, "words");
                    if (cJSON_IsArray(words) && cJSON_GetArraySize(words) > 0) {
                        cJSON* first_word = cJSON_GetArrayItem(words, 0);
                        cJSON* start_time = cJSON_GetObjectItem(first_word, "startTime");
                        cJSON* data_text = cJSON_GetObjectItem(first_word, "data");
                        
                        if (cJSON_IsNumber(start_time) && cJSON_IsString(data_text)) {
                            // Build full sentence from all words
                            std::string full_sentence;
                            int word_count = cJSON_GetArraySize(words);
                            for (int j = 0; j < word_count; j++) {
                                cJSON* word = cJSON_GetArrayItem(words, j);
                                cJSON* word_data = cJSON_GetObjectItem(word, "data");
                                if (cJSON_IsString(word_data)) {
                                    if (j > 0) full_sentence += " ";
                                    full_sentence += word_data->valuestring;
                                }
                            }
                            
                            lyrics_.push_back(std::make_pair(start_time->valueint, full_sentence));
                        }
                    }
                }
            }
            
            ESP_LOGI(TAG, "Parsed %d lyric lines", lyrics_.size());
            success = lyrics_.size() > 0;
        }
    }
    
    cJSON_Delete(json);
    return success;
}

void Esp32Music::LyricDisplayThread() {
    ESP_LOGI(TAG, "Lyric thread started");
    
    if (!GetLyrics(current_song_id_)) {
        ESP_LOGW(TAG, "Failed to get lyrics");
        is_lyric_running_ = false;
        return;
    }
    
    while (is_lyric_running_ && is_playing_) {
        UpdateLyricDisplay(current_play_time_ms_);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    ESP_LOGI(TAG, "Lyric thread finished");
}

void Esp32Music::UpdateLyricDisplay(int64_t current_time_ms) {
    std::lock_guard<std::mutex> lock(lyrics_mutex_);
    
    if (lyrics_.empty()) return;
    
    int new_index = -1;
    for (size_t i = 0; i < lyrics_.size(); i++) {
        if (current_time_ms >= lyrics_[i].first) {
            new_index = i;
        } else {
            break;
        }
    }
    
    if (new_index != current_lyric_index_ && new_index >= 0) {
        current_lyric_index_ = new_index;
        
        auto display = Board::GetInstance().GetDisplay();
        if (display) {
            std::string lyric_text = lyrics_[new_index].second;
            display->SetMusicInfo(lyric_text.c_str());
            ESP_LOGD(TAG, "Lyric: %s", lyric_text.c_str());
        }
    }
}

void Esp32Music::SetDisplayMode(DisplayMode mode) {
    display_mode_ = mode;
    ESP_LOGI(TAG, "Display mode set to: %s", 
             mode == DISPLAY_MODE_SPECTRUM ? "SPECTRUM" : "LYRICS");
}

std::string Esp32Music::GetCheckMusicServerUrl() {
    // Try to get URL from settings first (using "music_url" key for compatibility)
    std::string saved_url = Settings::GetString("music_url", "");
    if (!saved_url.empty()) {
        ESP_LOGI(TAG, "Using music server URL from settings: %s", saved_url.c_str());
        return saved_url;
    }
    
    // Fallback to default ZingMP3 URL
    ESP_LOGI(TAG, "Using default ZingMP3 URL: %s", DEFAULT_ZINGMP3_URL);
    return DEFAULT_ZINGMP3_URL;
}
