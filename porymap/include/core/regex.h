#pragma once
#ifndef REGEX_H
#define REGEX_H

namespace Regex {
    constexpr auto Pattern_INCBIN = R"(INC(BIN|GFX)_\w+?\s*\(\s*\"(?<path>[^\"]*)\"[^\)]*\))";
};

#endif // REGEX_H
