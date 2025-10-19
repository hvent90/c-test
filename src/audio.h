#ifndef AUDIO_H
#define AUDIO_H

void InitAudio();
void PlayEnemySpawnSound();
void PlayBounceSound();
void PlayBounceSoundWithVelocity(float velocity);
void CleanupAudio();

#endif