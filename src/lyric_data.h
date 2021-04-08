#pragma once

#include "stdafx.h"

#include "preferences.h"

// Raw (unparsed) lyric data
struct LyricDataRaw
{
    GUID source_id;
    std::string text;
};

// Parsed lyric data
struct LyricDataLine
{
    TCHAR* text;
    size_t text_length;
    double timestamp;
};

struct LyricData : LyricDataRaw
{
    std::vector<std::string> tags;
    std::vector<LyricDataLine> lines;

    LyricData() = default;
    LyricData(const LyricData& other) = delete;
    LyricData(LyricData&& other);

    bool IsTimestamped();
    bool IsEmpty();

    void operator =(const LyricData& other) = delete;
    void operator =(LyricData&& other);

    ~LyricData();
};
