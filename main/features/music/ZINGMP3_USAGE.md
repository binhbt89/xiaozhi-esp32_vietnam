# ESP32 ZingMP3 Integration - Usage Guide

## Overview

This implementation provides ESP32 integration with your ZingMP3 API server, allowing the device to search, stream, and display lyrics for Vietnamese music from Zing MP3.

## Files Created

- **[esp32_zingmp3.h](file:///d:/xiaozhi-esp32-main/xiaozhi-esp32-main/xiaozhi-esp32_vietnam/main/features/music/esp32_zingmp3.h)** - Header file with class definition
- **[esp32_zingmp3.cc](file:///d:/xiaozhi-esp32-main/xiaozhi-esp32-main/xiaozhi-esp32_vietnam/main/features/music/esp32_zingmp3.cc)** - Implementation file

## Configuration

### Server URL

**Default:** `http://192.168.1.100:5555`

You need to change this to match your ZingMP3 server address. Edit line 27 in `esp32_zingmp3.cc`:

```cpp
#define DEFAULT_ZINGMP3_URL "http://YOUR_SERVER_IP:5555"
```

Or set it at runtime:
```cpp
zingmp3_player->SetServerUrl("http://192.168.1.100:5555");
```

The URL can also be saved to persistent settings:
```cpp
Settings::SetString("zingmp3_url", "http://192.168.1.100:5555");
```

### Audio Quality

Three quality levels are supported:
- **QUALITY_128** (128kbps) - Always available
- **QUALITY_320** (320kbps) - Requires VIP account
- **QUALITY_LOSSLESS** (FLAC) - Requires VIP account

Set preferred quality:
```cpp
zingmp3_player->SetPreferredQuality(Esp32ZingMp3::QUALITY_128);
```

The implementation automatically falls back to 128kbps if higher quality is unavailable.

## Usage Example

### Basic Usage

```cpp
#include "esp32_zingmp3.h"

// Create player instance
Esp32ZingMp3* zingmp3_player = new Esp32ZingMp3();

// Initialize
zingmp3_player->Initialize();

// Play a song (searches and streams automatically)
zingmp3_player->Download("Lạc Trôi", "Sơn Tùng MTP");

// Stop playback
zingmp3_player->StopStreaming();
```

### Advanced Usage

```cpp
// Set server URL
zingmp3_player->SetServerUrl("http://192.168.1.100:5555");

// Set quality preference
zingmp3_player->SetPreferredQuality(Esp32ZingMp3::QUALITY_128);

// Set display mode to show lyrics
zingmp3_player->SetDisplayMode(Esp32ZingMp3::DISPLAY_MODE_LYRICS);

// Play song
zingmp3_player->Download("Nơi Này Có Anh", "");

// Check playback status
if (zingmp3_player->IsPlaying()) {
    ESP_LOGI(TAG, "Music is playing");
}

// Get buffer status
size_t buffer_size = zingmp3_player->GetBufferSize();
```

## API Call Flow

When you call `Download("Lạc Trôi", "Sơn Tùng MTP")`, the following happens:

1. **Search** → `GET /api/search?q=Lạc+Trôi+Sơn+Tùng+MTP`
   - Finds the song and extracts the song ID

2. **Get Song Info** → `GET /api/info-song?id=<songId>`
   - Retrieves title, artist, duration

3. **Get Stream URL** → `GET /api/song?id=<songId>`
   - Gets the MP3 streaming URL (128kbps by default)

4. **Start Streaming** → Downloads and plays the MP3 stream
   - Uses existing MP3 decoder and audio codec

5. **Get Lyrics** (if lyric mode enabled) → `GET /api/lyric?id=<songId>`
   - Downloads and displays synchronized lyrics

## Display Modes

### Spectrum Mode (Default)
Shows audio spectrum visualization with song info:
```cpp
zingmp3_player->SetDisplayMode(Esp32ZingMp3::DISPLAY_MODE_SPECTRUM);
```

Display shows:
```
ZINGMP3 《Lạc Trôi》
Sơn Tùng M-TP • Đang phát...
```

### Lyrics Mode
Shows synchronized lyrics:
```cpp
zingmp3_player->SetDisplayMode(Esp32ZingMp3::DISPLAY_MODE_LYRICS);
```

Lyrics are automatically synchronized with playback time.

## Integration with Existing Code

This implementation follows the same `Music` interface as `Esp32Music`, so you can use it as a drop-in replacement:

```cpp
// Instead of:
Music* music = new Esp32Music();

// Use:
Music* music = new Esp32ZingMp3();
```

All methods from the `Music` interface are supported:
- `Download(song_name, artist_name)`
- `StartStreaming(url)`
- `StopStreaming()`
- `IsPlaying()`
- `IsDownloading()`
- `GetBufferSize()`
- `GetAudioData()`

## Error Handling

The implementation handles various error cases:

### No Search Results
```
E (12345) Esp32ZingMp3: No songs found for query: Unknown Song
```

### VIP Content
If you request 320kbps or lossless without VIP, it automatically falls back to 128kbps:
```
W (12345) Esp32ZingMp3: Quality 320 not available, trying 128kbps
I (12346) Esp32ZingMp3: Using 128kbps fallback
```

### Network Errors
```
E (12345) Esp32ZingMp3: Failed to connect to ZingMP3 search API
```

### No Lyrics Available
```
W (12345) Esp32ZingMp3: Failed to get lyrics
```

## Memory Management

The implementation uses:
- **SPIRAM** for audio buffers (256KB max)
- **Heap** for JSON parsing
- **Thread-safe** buffer management with mutex and condition variables

All memory is properly freed in the destructor.

## Threading

Three threads are used:
1. **Download Thread** - Downloads MP3 stream chunks
2. **Playback Thread** - Decodes MP3 and plays audio
3. **Lyric Thread** - Updates synchronized lyrics (when enabled)

All threads are properly synchronized and cleaned up on stop/destroy.

## Compilation

Add to your CMakeLists.txt or component configuration:

```cmake
set(COMPONENT_SRCS
    # ... existing sources ...
    "features/music/esp32_zingmp3.cc"
)

set(COMPONENT_ADD_INCLUDEDIRS
    # ... existing includes ...
    "features/music"
)
```

## Testing Checklist

Before deploying:

- [ ] Update `DEFAULT_ZINGMP3_URL` to your server IP
- [ ] Ensure ZingMP3 server is running and accessible
- [ ] Test network connectivity from ESP32 to server
- [ ] Test with popular Vietnamese songs
- [ ] Verify audio playback quality
- [ ] Test lyric synchronization (if using lyric mode)
- [ ] Check serial logs for errors

## Example Serial Output

Successful playback:
```
I (12345) Esp32ZingMp3: Initializing ZingMP3 player
I (12346) Esp32ZingMp3: Server URL: http://192.168.1.100:5555
I (12456) Esp32ZingMp3: Download request: Lạc Trôi - Sơn Tùng MTP
I (12567) Esp32ZingMp3: Searching for: Lạc Trôi Sơn Tùng MTP
I (12678) Esp32ZingMp3: Found top result: Lạc Trôi - Sơn Tùng M-TP (ID: ZW6AOCAZ)
I (12789) Esp32ZingMp3: Getting song info for ID: ZW6AOCAZ
I (12890) Esp32ZingMp3: Song info: Lạc Trôi - Sơn Tùng M-TP (240s)
I (13001) Esp32ZingMp3: Getting stream URL for ID: ZW6AOCAZ, quality: 128
I (13112) Esp32ZingMp3: Got stream URL for quality 128
I (13223) Esp32ZingMp3: Starting playback: Lạc Trôi - Sơn Tùng M-TP
I (13334) Esp32ZingMp3: Starting streaming: https://...
I (13445) Esp32ZingMp3: Streaming threads started
I (13556) Esp32ZingMp3: Download started, status: 200
I (13667) Esp32ZingMp3: Detected MP3 with ID3 tag
I (13778) Esp32ZingMp3: Starting playback with buffer: 32768
I (13889) Esp32ZingMp3: FFT started
```

## Troubleshooting

### "Failed to connect to ZingMP3 search API"
- Check server URL is correct
- Verify server is running: `curl http://YOUR_IP:5555/health`
- Check network connectivity from ESP32

### "No songs found for query"
- Try a more specific search query
- Check if the song exists on Zing MP3
- Verify server API is working: `curl "http://YOUR_IP:5555/api/search?q=test"`

### "No stream URL found"
- Song may require VIP subscription
- Check server logs for API errors
- Try with a different song

### Audio playback issues
- Check audio codec configuration
- Verify speaker/amplifier connections
- Check sample rate settings

## Next Steps

1. Update the server URL in the code
2. Compile and flash to ESP32
3. Test with your favorite Vietnamese songs
4. Integrate into your main application
5. Consider adding playlist support
6. Add user interface for song selection

## Differences from Original esp32_music.cc

| Feature | esp32_music.cc | esp32_zingmp3.cc |
|---------|----------------|------------------|
| Server | Simple custom API | ZingMP3 REST API |
| Search | Direct song+artist | Full search with results |
| Authentication | Custom ESP32 auth | No authentication |
| Quality | Fixed | Selectable (128/320/lossless) |
| Lyrics | Simple LRC | Synchronized sentences |
| API Calls | 1 endpoint | Multiple endpoints |

Both implementations share the same MP3 decoding and playback infrastructure.
