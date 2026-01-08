#ifndef ESP32_MUSIC_H
#define ESP32_MUSIC_H

#include <string>
#include <thread>
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <vector>

#include "music.h"

// MP3 decoder support
extern "C" {
#include "mp3dec.h"
}

// Audio data chunk structure
struct AudioChunk {
    uint8_t* data;
    size_t size;
    
    AudioChunk() : data(nullptr), size(0) {}
    AudioChunk(uint8_t* d, size_t s) : data(d), size(s) {}
};

// Song information structure
struct SongInfo {
    std::string encodeId;
    std::string title;
    std::string artist;
    int duration;
    std::string thumbnailUrl;
    
    SongInfo() : duration(0) {}
};

class Esp32Music : public Music {
public:
    // Display mode control
    enum DisplayMode {
        DISPLAY_MODE_SPECTRUM = 0,  // Default: display spectrum
        DISPLAY_MODE_LYRICS = 1     // Display lyrics
    };
    
    // Audio quality options
    enum AudioQuality {
        QUALITY_128 = 0,    // 128kbps (always available)
        QUALITY_320 = 1,    // 320kbps (VIP required)
        QUALITY_LOSSLESS = 2 // FLAC (VIP required)
    };

private:
    std::string zingmp3_server_url_;
    std::string last_downloaded_data_;
    std::string current_music_url_;
    std::string current_song_id_;
    SongInfo current_song_info_;
    bool song_name_displayed_;
    bool full_info_displayed_;
    bool fft_started_;
    AudioQuality preferred_quality_;
    
    // Lyrics-related
    std::string current_lyric_url_;
    std::vector<std::pair<int, std::string>> lyrics_;  // Timestamp (ms) and lyric text
    std::mutex lyrics_mutex_;
    std::atomic<int> current_lyric_index_;
    std::thread lyric_thread_;
    std::atomic<bool> is_lyric_running_;
    
    std::atomic<DisplayMode> display_mode_;
    std::atomic<bool> is_playing_;
    std::atomic<bool> is_downloading_;
    std::thread play_thread_;
    std::thread download_thread_;
    int64_t current_play_time_ms_;
    int64_t last_frame_time_ms_;
    int total_frames_decoded_;

    // Audio buffer
    std::queue<AudioChunk> audio_buffer_;
    std::mutex buffer_mutex_;
    std::condition_variable buffer_cv_;
    size_t buffer_size_;
    static constexpr size_t MAX_BUFFER_SIZE = 256 * 1024;  // 256KB buffer
    static constexpr size_t MIN_BUFFER_SIZE = 32 * 1024;   // 32KB minimum playback buffer
    
    // MP3 decoder-related
    HMP3Decoder mp3_decoder_;
    MP3FrameInfo mp3_frame_info_;
    bool mp3_decoder_initialized_;
    
    // Private methods
    void DownloadAudioStream(const std::string& music_url);
    void PlayAudioStream();
    void ClearAudioBuffer();
    bool InitializeMp3Decoder();
    void CleanupMp3Decoder();
    void ResetSampleRate();
    
    // ZingMP3 API methods
    bool SearchSong(const std::string& query, SongInfo& song_info);
    bool GetSongInfo(const std::string& song_id, SongInfo& song_info);
    bool GetStreamUrl(const std::string& song_id, AudioQuality quality, std::string& stream_url);
    bool GetLyrics(const std::string& song_id);
    
    // Lyrics-related private methods
    bool ParseZingMp3Lyrics(const std::string& lyric_json);
    void LyricDisplayThread();
    void UpdateLyricDisplay(int64_t current_time_ms);
    
    // ID3 tag handling
    size_t SkipId3Tag(uint8_t* data, size_t size);
    
    // Helper methods
    std::string GetQualityString(AudioQuality quality);

    int16_t* final_pcm_data_fft = nullptr;

public:
    Esp32Music();
    ~Esp32Music();

    void Initialize();

    // Override Music interface methods
    virtual bool Download(const std::string& song_name, const std::string& artist_name) override;
    virtual std::string GetDownloadResult() override;
    virtual bool StartStreaming(const std::string& music_url) override;
    virtual bool StopStreaming() override;
    virtual size_t GetBufferSize() const override { return buffer_size_; }
    virtual bool IsDownloading() const override { return is_downloading_; }
    virtual int16_t* GetAudioData() override { return final_pcm_data_fft; }
    virtual bool IsPlaying() const override { return is_playing_; }
    
    // Display mode control methods
    void SetDisplayMode(DisplayMode mode);
    DisplayMode GetDisplayMode() const { return display_mode_.load(); }
    
    // Quality control methods
    void SetPreferredQuality(AudioQuality quality) { preferred_quality_ = quality; }
    AudioQuality GetPreferredQuality() const { return preferred_quality_; }
    
    // Server URL configuration
    void SetServerUrl(const std::string& url) { zingmp3_server_url_ = url; }
    std::string GetServerUrl() const { return zingmp3_server_url_; }
    std::string GetCheckMusicServerUrl();  // Get URL from settings or default
};

#endif // ESP32_MUSIC_H
