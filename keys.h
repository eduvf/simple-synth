#include "raylib.h"

struct key
{
    KeyboardKey k;
    int midi;
};

struct key keys[] = {
    {KEY_Z, 48},     {KEY_S, 49},   {KEY_X, 50},    {KEY_D, 51},
    {KEY_C, 52},     {KEY_V, 53},   {KEY_G, 54},    {KEY_B, 55},
    {KEY_H, 56},     {KEY_N, 57},   {KEY_J, 58},    {KEY_M, 59},
    {KEY_COMMA, 60}, {KEY_Q, 60},   {KEY_TWO, 61},  {KEY_W, 62},
    {KEY_THREE, 63}, {KEY_E, 64},   {KEY_R, 65},    {KEY_FIVE, 66},
    {KEY_T, 67},     {KEY_SIX, 68}, {KEY_Y, 69},    {KEY_SEVEN, 70},
    {KEY_U, 71},     {KEY_I, 72},   {KEY_NINE, 73}, {KEY_O, 74},
    {KEY_ZERO, 75},  {KEY_P, 76},
};