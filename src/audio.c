#include "raylib.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

static AudioStream spawnStream;
static AudioStream bounceStream;
static float spawnSineIdx = 0.0f;
static float spawnSineIdx2 = 0.0f; // Second harmonic
static float bounceSineIdx = 0.0f;
static bool isPlaying = false;
static bool bouncePlaying = false;
static int frameCount = 0;
static int bounceFrameCount = 0;
static float baseFrequency = 880.0f;
static float bounceVelocity = 0.0f;

void SpawnAudioCallback(void *buffer, unsigned int frames) {
    if (!isPlaying) {
        memset(buffer, 0, frames * sizeof(short) * 2); // Stereo buffer
        return;
    }
    
    short *d = (short *)buffer;
    float time = (float)frameCount / 44100.0f;
    
    // Frequency sweep with exponential decay
    float frequency = baseFrequency * exp(-time * 6.0f);
    
    // Amplitude envelope (quick attack, exponential decay)
    float amplitude = exp(-time * 8.0f);
    
    // Add subtle vibrato
    float vibrato = sinf(time * 20.0f) * 0.02f;
    frequency *= (1.0f + vibrato);
    
    // Stereo panning that shifts during playback
    float pan = sinf(time * 15.0f) * 0.3f;
    
    float incr1 = frequency / 44100.0f;
    float incr2 = (frequency * 1.5f) / 44100.0f; // 1.5x harmonic
    
    for (unsigned int i = 0; i < frames; i++) {
        // Mix fundamental and harmonic for richer sound
        float sample = sinf(2 * PI * spawnSineIdx) * 0.7f + 
                      sinf(2 * PI * spawnSineIdx2) * 0.3f;
        
        // Apply amplitude envelope
        sample *= amplitude * 12000.0f;
        
        // Apply stereo panning
        float leftPan = 1.0f - pan;
        float rightPan = 1.0f + pan;
        
        // Write to stereo buffer (interleaved LRLRLR...)
        d[i * 2] = (short)(sample * leftPan);     // Left channel
        d[i * 2 + 1] = (short)(sample * rightPan); // Right channel
        
        spawnSineIdx += incr1;
        spawnSineIdx2 += incr2;
        
        if (spawnSineIdx > 1.0f) spawnSineIdx -= 1.0f;
        if (spawnSineIdx2 > 1.0f) spawnSineIdx2 -= 1.0f;
    }
    
    // Auto-stop after short duration
    frameCount += frames;
    if (frameCount > 5280) { // ~0.12 seconds at 44100Hz
        isPlaying = false;
        frameCount = 0;
    }
}

void BounceAudioCallback(void *buffer, unsigned int frames) {
    if (!bouncePlaying) {
        memset(buffer, 0, frames * sizeof(short) * 2); // Stereo buffer
        return;
    }
    
    short *d = (short *)buffer;
    float time = (float)bounceFrameCount / 44100.0f;
    
    // Velocity affects base frequency (faster = higher pitch)
    float baseFreq = 150.0f + bounceVelocity * 50.0f; // 150-650Hz range
    float frequency = baseFreq * exp(-time * 15.0f); // Quick decay
    
    // Velocity affects amplitude (faster = louder)
    float baseAmplitude = 0.3f + bounceVelocity * 0.1f; // Scale with velocity
    float amplitude = baseAmplitude * exp(-time * 20.0f);
    
    // Add some noise for impact feel (more noise for faster impacts)
    float noiseAmount = 0.05f + bounceVelocity * 0.05f;
    float noise = ((float)rand() / RAND_MAX - 0.5f) * noiseAmount;
    
    float incr = frequency / 44100.0f;
    
    for (unsigned int i = 0; i < frames; i++) {
        // Mix sine wave with noise for impact sound
        float sample = sinf(2 * PI * bounceSineIdx) * 0.8f + noise;
        
        // Apply amplitude envelope with velocity scaling
        sample *= amplitude * 12000.0f;
        
        // Center the sound (no panning for bounce)
        d[i * 2] = (short)sample;     // Left channel
        d[i * 2 + 1] = (short)sample; // Right channel
        
        bounceSineIdx += incr;
        if (bounceSineIdx > 1.0f) bounceSineIdx -= 1.0f;
    }
    
    // Auto-stop after very short duration
    bounceFrameCount += frames;
    if (bounceFrameCount > 2205) { // ~0.05 seconds at 44100Hz
        bouncePlaying = false;
        bounceFrameCount = 0;
    }
}

void InitAudio() {
    InitAudioDevice();
    spawnStream = LoadAudioStream(44100, 16, 2); // 2 channels for stereo
    bounceStream = LoadAudioStream(44100, 16, 2); // 2 channels for stereo
    SetAudioStreamCallback(spawnStream, SpawnAudioCallback);
    SetAudioStreamCallback(bounceStream, BounceAudioCallback);
    PlayAudioStream(spawnStream);
    PlayAudioStream(bounceStream);
}

void PlayEnemySpawnSound() {
    isPlaying = true;
    spawnSineIdx = 0.0f;
    spawnSineIdx2 = 0.0f;
    baseFrequency = 880.0f + (float)(GetRandomValue(-50, 50)); // Randomize pitch slightly
    frameCount = 0; // Reset frame counter for each new spawn
}

void PlayBounceSound() {
    bouncePlaying = true;
    bounceSineIdx = 0.0f;
    bounceFrameCount = 0; // Reset frame counter for each bounce
}

void PlayBounceSoundWithVelocity(float velocity) {
    bounceVelocity = fabsf(velocity); // Use absolute velocity
    bouncePlaying = true;
    bounceSineIdx = 0.0f;
    bounceFrameCount = 0; // Reset frame counter for each bounce
}

void CleanupAudio() {
    UnloadAudioStream(spawnStream);
    UnloadAudioStream(bounceStream);
    CloseAudioDevice();
}