#pragma once
#include <string>
#include <unordered_map>
#include <cctype>

namespace dsp {

class MorseTable {
public:
    static std::string encode_char(char c) {
        c = std::toupper(static_cast<unsigned char>(c));
        auto it = get_char_to_morse().find(c);
        if (it != get_char_to_morse().end()) {
            return it->second;
        }
        return "";
    }

    static char decode_morse(const std::string& pattern) {
        auto it = get_morse_to_char().find(pattern);
        if (it != get_morse_to_char().end()) {
            return it->second;
        }
        return '?';
    }

private:
    static const std::unordered_map<char, std::string>& get_char_to_morse() {
        static std::unordered_map<char, std::string> m = {
            {'A', ".-"},    {'B', "-..."},  {'C', "-.-."},  {'D', "-.."},
            {'E', "."},     {'F', "..-."},  {'G', "--."},   {'H', "...."},
            {'I', ".."},    {'J', ".---"},  {'K', "-.-"},   {'L', ".-.."},
            {'M', "--"},    {'N', "-."},    {'O', "---"},   {'P', ".--."},
            {'Q', "--.-"},  {'R', ".-."},   {'S', "..."},   {'T', "-"},
            {'U', "..-"},   {'V', "...-"},  {'W', ".--"},   {'X', "-..-"},
            {'Y', "-.--"},  {'Z', "--.."},  {'0', "-----"}, {'1', ".----"},
            {'2', "..---"}, {'3', "...--"}, {'4', "....-"}, {'5', "....."},
            {'6', "-...."}, {'7', "--..."}, {'8', "---.."}, {'9', "----."},
            {'.', ".-.-.-"},{',', "--..--"},{'?', "..--.."},{'/', "-..-."},
            {'=', "-...-"}, {'+', ".-.-."}, {'-', "-....-"},{' ', " "}
        };
        return m;
    }

    static const std::unordered_map<std::string, char>& get_morse_to_char() {
        static std::unordered_map<std::string, char> rev;
        static bool init = false;
        if (!init) {
            for (const auto& kv : get_char_to_morse()) {
                if (kv.first != ' ') {
                    rev[kv.second] = kv.first;
                }
            }
            init = true;
        }
        return rev;
    }
};

} // namespace dsp
