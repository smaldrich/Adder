#pragma once

#ifdef __cplusplus

extern "C" void sound_init();
extern "C" void sound_deinit();
extern "C" float sound_get();

#else

void sound_init();
void sound_deinit();
float sound_get();

#endif