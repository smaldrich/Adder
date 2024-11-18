#include "snooze.h"

snzr_Font style_titleFont;
snzr_Font style_paragraphFont;
snzr_Font style_labelFont;

#define STYLE_TEXT_COLOR HMM_V4(60 / 255.0, 60 / 255.0, 60 / 255.0, 1)
#define STYLE_ACCENT_COLOR HMM_V4(221 / 255.0, 255 / 255.0, 178 / 255.0, 1)
#define STYLE_BACKGROUND_COLOR HMM_V4(1, 1, 1, 1)
#define STYLE_BORDER_THICKNESS 4

// font loading from main.c