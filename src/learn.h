// Short, plain-language lessons for first-time budgeters (the Learn tab and "?" help).
#pragma once
#include <vector>

struct Lesson { const char *title; const char *body; };
const std::vector<Lesson> &lessons();

// One-screen explanation for the "?" key on each tab
const char *screen_help(int tab);
