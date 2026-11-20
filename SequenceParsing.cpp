/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of Natron <https://natrongithub.github.io/>,
 * (C) 2018-2022 The Natron developers
 * (C) 2013-2018 INRIA and Alexandre Gauthier-Foichat
 *
 * Natron is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Natron is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this.  If not, see <http://www.gnu.org/licenses/gpl-2.0.html>
 * ***** END LICENSE BLOCK ***** */

#include "SequenceParsing.h"

#include <cassert>
#include <cmath>
#include <climits>
#include <cstddef>
#ifdef DEBUG
#include <iostream>
#endif
#include <stdexcept>
#include <istream>
#include <algorithm>
#include <memory>

#if (defined(_WIN32) || defined(__WIN32__) || defined(WIN32))
#include <windows.h>
#else
#include <sys/stat.h>
#endif

#include "tinydir/tinydir.h"


// Use: #pragma message WARN("My message")
#if _MSC_VER
#   define FILE_LINE_LINK __FILE__ "(" STRINGISE(__LINE__) ") : "
#   define WARN(exp) (FILE_LINE_LINK "WARNING: " exp)
#else//__GNUC__ - may need other defines for different compilers
#   define WARN(exp) ("WARNING: " exp)
#endif

///the maximum number of non existing frame before Natron gives up trying to figure out a sequence layout.
#define NATRON_DIALOG_MAX_SEQUENCES_HOLE 1000

using std::size_t;
using std::map;
using std::string;
using std::wstring;
using std::vector;
using std::pair;
using std::make_pair;

using namespace SequenceParsing;

namespace  {

// Locale-independent ASCII character classes.
// Never pass a plain char to the <cctype> functions: bytes of UTF-8 sequences are
// negative when char is signed, which is undefined behaviour (and an assertion
// failure with the MSVC debug runtime).
static inline bool
isAsciiDigit(char c)
{
    return c >= '0' && c <= '9';
}

static inline bool
isAsciiAlpha(char c)
{
    return ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' );
}

static inline char
asciiToLower(char c)
{
    return ( c >= 'A' && c <= 'Z' ) ? char(c - 'A' + 'a') : c;
}

static inline char
asciiToUpper(char c)
{
    return ( c >= 'a' && c <= 'z' ) ? char(c - 'a' + 'A') : c;
}

// Returns the first character (code point) of a UTF-8 string, upper-cased if it is ASCII.
static string
firstCharacterUpper(const string& str)
{
    if ( str.empty() ) {
        return string();
    }
    size_t len = 1;
    // include the UTF-8 continuation bytes (10xxxxxx)
    while ( len < str.size() && ( ( (unsigned char)str[len] & 0xC0 ) == 0x80 ) ) {
        ++len;
    }
    string ret = str.substr(0, len);
    ret[0] = asciiToUpper(ret[0]);

    return ret;
}

#if (defined(_WIN32) || defined(__WIN32__) || defined(WIN32))
static wstring
utf8_to_utf16(const string& str)
{
    wstring native;

    native.resize( MultiByteToWideChar (CP_UTF8, 0, str.data(), str.length(), NULL, 0) );
    MultiByteToWideChar ( CP_UTF8, 0, str.data(), str.length(), &native[0], (int)native.size() );

    return native;
}     // utf8_to_utf16

static string
utf16_to_utf8 (const wstring& str)
{
    string utf8;

    utf8.resize(WideCharToMultiByte (CP_UTF8, 0, str.data(), str.length(), NULL, 0, NULL, NULL));
    WideCharToMultiByte (CP_UTF8, 0, str.data(), str.length(), &utf8[0], (int)utf8.size(), NULL, NULL);

    return utf8;
}
#endif


static std::size_t
getFileSize(const string& filename)
{
#if (defined(_WIN32) || defined(__WIN32__) || defined(WIN32))
    wstring wfilename = utf8_to_utf16(filename);
    LARGE_INTEGER file_size;
    file_size.QuadPart = 0;

    /*
       On Windows there are 3 methods to get the size of a file, the most robust being the 1st one
       but it is also the most expensive.
       By order of performance: 3), 2), 1)
       By order of reliability: 1), 2), 3)
       Since in our use-case here the file size is just a hint, use the fastest alternative.
     */
    /*
       //Method 1, open the file
         HANDLE file = CreateFileW(wfilename.c_str(),
                                  GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
         if(file && file != INVALID_HANDLE_VALUE){
             if(!GetFileSizeEx(file, &file_size)){
                 file_size.QuadPart = 0; // clean-up on fail
             }
             CloseHandle(file);
         }
     */

    /*
       //Method 2, find the file
        WIN32_FIND_DATAW find_data;
        HANDLE find_file = FindFirstFileW(wfilename.c_str(), &find_data);
        if(find_file && find_file != INVALID_HANDLE_VALUE){
            file_size.LowPart = find_data.nFileSizeLow;
            file_size.HighPart = find_data.nFileSizeHigh;
            FindClose(find_file);
        }
     */

    //Method 3, read the file attributes, this is the fastest
    WIN32_FILE_ATTRIBUTE_DATA file_attr_data;
    if ( GetFileAttributesExW(wfilename.c_str(), GetFileExInfoStandard, &file_attr_data) ) {
        file_size.LowPart = file_attr_data.nFileSizeLow;
        file_size.HighPart = file_attr_data.nFileSizeHigh;
    }

    return (std::size_t)file_size.QuadPart;
#else // !(defined(_WIN32) || defined(__WIN32__) || defined(WIN32))
    struct stat st;
    if ( (stat(filename.c_str(), &st) != 0) || (st.st_size < 0) ) {
        return 0; // the size is just a hint
    }

    return (std::size_t)st.st_size;
#endif // (defined(_WIN32) || defined(__WIN32__) || defined(WIN32))
}

#if 0
// case-insensitive char_traits
// see http://www.gotw.ca/gotw/029.htm
// in order to use case insensitive search and compare, all functions should
// be templated and use basic_string<>:
// template<class _CharT, class _Traits, class _Allocator>
// void function(basic_string<_CharT, _Traits, _Allocator>& s, ...)
//
struct ci_char_traits
    : public std::char_traits<char>
{
    static bool eq(char c1,
                   char c2) { return toupper(c1) == toupper(c2); }

    static bool ne(char c1,
                   char c2) { return toupper(c1) != toupper(c2); }

    static bool lt(char c1,
                   char c2) { return toupper(c1) <  toupper(c2); }

    static int compare(const char* s1,
                       const char* s2,
                       size_t n)
    {
        while (n-- != 0) {
            if ( toupper(*s1) < toupper(*s2) ) { return -1; }
            if ( toupper(*s1) > toupper(*s2) ) { return 1; }
            ++s1; ++s2;
        }

        return 0;
    }

    static const char* find(const char* s,
                            int n,
                            char a)
    {
        while ( n-- > 0 && toupper(*s) != toupper(a) ) {
            ++s;
        }

        return s;
    }
};

typedef std::basic_string<char, ci_char_traits> ci_string;
#endif

/**
 * @brief Given the pattern unpathed without extension (e.g: "filename###") and the file extension (e.g "jpg") ; this
 * functions extracts the common parts and the variables of the pattern ordered from left to right .
 * For example: file%04dname### and the jpg extension would return:
 * 3 common parts: "file","name",".jpg"
 * 2 variables: "%04d", "###"
 * The variables by order vector's second member is an int indicating how many non-variable (chars belonging to common parts) characters
 * were found before this variable.
 **/
static bool
extractCommonPartsAndVariablesFromPattern(const string& patternUnPathedWithoutExt,
                                          const string& patternExtension,
                                          StringList* commonParts,
                                          vector<pair<string, int> >* variablesByOrder)
{
    bool inPrintfLikeArg = false;
    int printfLikeArgIndex = 0;

    string commonPart;
    string variable;
    int commonCharactersFound = 0;
    bool previousCharIsSharp = false;
    for (int i = 0; i < (int)patternUnPathedWithoutExt.size(); ++i) {
        const char& c = patternUnPathedWithoutExt[i];
        if (c == '#') {
            if ( !commonPart.empty() ) {
                commonParts->push_back(commonPart);
                commonCharactersFound += commonPart.size();
                commonPart.clear();
            }
            if ( !previousCharIsSharp && !variable.empty() ) {
                variablesByOrder->push_back( make_pair(variable, commonCharactersFound) );
                variable.clear();
            }
            variable.push_back(c);
            previousCharIsSharp = true;
        } else if (c == '%') {
            char next = '\0';
            if (i < (int)patternUnPathedWithoutExt.size() - 1) {
                next = patternUnPathedWithoutExt[i + 1];
            }
            char prev = '\0';
            if (i > 0) {
                prev = patternUnPathedWithoutExt[i - 1];
            }

            if (next == '\0') {
                ///if we're at end, just consider the % character as any other
                commonPart.push_back(c);
            } else if (prev == '%') {
                ///we escaped the previous %, append this one to the text
                commonPart.push_back(c);
            } else if (next != '%') {
                ///if next == % then we have escaped the character
                ///we don't support nested  variables
                if (inPrintfLikeArg) {
                    return false;
                }
                printfLikeArgIndex = 0;
                inPrintfLikeArg = true;
                if ( !commonPart.empty() ) {
                    commonParts->push_back(commonPart);
                    commonCharactersFound += commonPart.size();
                    commonPart.clear();
                }
                if ( !variable.empty() ) {
                    variablesByOrder->push_back( make_pair(variable, commonCharactersFound) );
                    variable.clear();
                }
                variable.push_back(c);
            }
        } else if ( ( ( c == 'd') || ( c == 'v') || ( c == 'V') ) && inPrintfLikeArg ) {
            inPrintfLikeArg = false;
            assert( !variable.empty() );
            variable.push_back(c);
            variablesByOrder->push_back( make_pair(variable, commonCharactersFound) );
            variable.clear();
        } else if (inPrintfLikeArg) {
            ++printfLikeArgIndex;
            assert( !variable.empty() );
            variable.push_back(c);
            ///if we're after a % character, and c is a letter different than d or v or V
            ///or c is digit different than 0, then we don't support this printf like style.
            if ( isAsciiAlpha(c) ||
                 ( ( printfLikeArgIndex == 1) && ( c != '0') ) ) {
                commonParts->push_back(variable);
                commonCharactersFound += variable.size();
                variable.clear();
                inPrintfLikeArg = false;
            }
        } else {
            commonPart.push_back(c);
            if ( !variable.empty() ) {
                variablesByOrder->push_back( make_pair(variable, commonCharactersFound) );
                variable.clear();
            }
        }
    }

    if ( !commonPart.empty() ) {
        commonParts->push_back(commonPart);
        commonCharactersFound += commonPart.size();
    }
    if ( !variable.empty() ) {
        variablesByOrder->push_back( make_pair(variable, commonCharactersFound) );
    }

    if ( !patternExtension.empty() ) {
        commonParts->push_back( string('.' + patternExtension) );
    }

    return true;
} // extractCommonPartsAndVariablesFromPattern

static size_t
findStr(const string& from,
        const string& toSearch,
        int pos)
{
    return from.find(toSearch, pos);
    // case insensitive version:
    //return ci_string(from.c_str()).find(toSearch.c_str(), pos);
}

static bool
startsWith(const string& str,
           const string& prefix)
{
    return str.substr( 0, prefix.size() ) == prefix;
    // case insensitive version:
    //return ci_string(str.substr(0,prefix.size()).c_str()) == prefix.c_str();
}

static bool
endsWith(const string& str,
         const string& suffix)
{
    return ( ( str.size() >= suffix.size() ) &&
             (str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0) );
}

static void
removeAllOccurences(string& str,
                    const string& toRemove)
{
    if ( str.size() ) {
        for ( size_t i = findStr(str, toRemove, 0);
              i != string::npos;
              i = findStr(str, toRemove, i) ) {
            str.erase( i, toRemove.size() );
        }
    }
}

/**
 * @brief Converts a string of ASCII digits to a non-negative int.
 * Returns false if the string is empty, contains anything else than digits,
 * or if the number does not fit in an int (std::atoi has undefined behaviour then).
 **/
static bool
stringToInt(const string& str,
            int* ret)
{
    if ( str.empty() ) {
        return false;
    }
    int value = 0;
    for (size_t i = 0; i < str.size(); ++i) {
        if ( !isAsciiDigit(str[i]) ) {
            return false;
        }
        int digit = str[i] - '0';
        if ( value > (INT_MAX - digit) / 10 ) {
            return false;
        }
        value = value * 10 + digit;
    }
    *ret = value;

    return true;
}

// std::to_string, unlike a stringstream, does not depend on the global C++ locale
// (which may have digit grouping, e.g. "1,234").
static string
stringFromInt(int nb)
{
    return std::to_string(nb);
}

static string
removeFileExtension(string& filename)
{
    size_t lastdot = filename.find_last_of(".");

    if (lastdot == string::npos) {
        return "";
    }
    string extension = filename.substr(lastdot + 1);
    filename = filename.substr(0, lastdot);

    return extension;
}

static void
getFilesFromDir(tinydir_dir& dir,
                StringList* ret)
{
    ///iterate through all the files in the directory
    while (dir.has_next) {
        tinydir_file file;
        int status = tinydir_readfile(&dir, &file);
        if ( ( status == 0) && !file.is_dir ) {
#if (defined(_WIN32) || defined(__WIN32__) || defined(WIN32)) && defined(UNICODE)
            wstring wfilename(file.name);
            string filename(utf16_to_utf8(wfilename));
#else
            string filename(file.name);
#endif
            if ( ( filename != ".") && ( filename != "..") ) {
                ret->push_back(filename);
            }
        }

        tinydir_next(&dir);
    }
}

/*
   The following rules applying for matching frame numbers:
   - If the number has at least as many digits as the digitsCount then it is OK
   - If the number has more digits than the digitsCOunt, it is only OK if it has 0 leading zeroes
 */
static bool
numberMatchDigits(int digitsCount,
                  const string& number,
                  int *frameNumber)
{
    if ( number.empty() ) {
        return false;
    }
    if (digitsCount < 1) {
        // %d (or %0d): no padding, like printf, so the number has at least one digit.
        digitsCount = 1;
    }

    if ( !stringToInt(number, frameNumber) ) {
        return false;
    }

    if ( (int)number.size() == digitsCount ) {
        return true;
    }

    if ( (int)number.size() < digitsCount ) {
        return false;
    }

    assert( (int)number.size() > digitsCount );

    if (number[0] == '0') {
        return false;
    }

    return true;
}

/*
   Matches a frame number written with a padding of digitsCount (### or %0<digitsCount>d)
   at startingPos in filename, i.e. what printf("%0<digitsCount>d") writes:
   the number is at least digitsCount characters long, and longer only without leading zeroes.
   A negative number starts with '-', which counts in the width, as with printf:
   -5 is written "-005" with a padding of 4.
 */
static bool
matchesFrameNumber(int digitsCount,
                   const string& filename,
                   size_t startingPos,
                   size_t *endPos,
                   int* frameNumber)
{
    bool negative = ( startingPos < filename.size() ) && ( filename[startingPos] == '-' );
    size_t variableIt = negative ? startingPos + 1 : startingPos;
    string variable;
    for (; variableIt < filename.size() && isAsciiDigit(filename[variableIt]); ++variableIt) {
        variable.push_back(filename[variableIt]);
    }
    *endPos = variableIt;

    if (!negative) {
        return numberMatchDigits(digitsCount, variable, frameNumber);
    }
    int absFrameNumber;
    if ( !numberMatchDigits(digitsCount - 1, variable, &absFrameNumber) || (absFrameNumber == 0) ) {
        return false;
    }
    *frameNumber = -absFrameNumber;

    return true;
}

/*
   Writes frameNumber as printf("%0<digitsCount>d") does.
 */
static string
frameNumberToString(int frameNumber,
                    int digitsCount)
{
    long long absFrameNumber = frameNumber;
    if (absFrameNumber < 0) {
        absFrameNumber = -absFrameNumber;
    }
    string ret = std::to_string(absFrameNumber);
    int width = (frameNumber < 0) ? digitsCount - 1 : digitsCount;
    if ( (int)ret.size() < width ) {
        ret.insert(0, width - ret.size(), '0');
    }
    if (frameNumber < 0) {
        ret.insert(0, 1, '-');
    }

    return ret;
}

// Does str contain prefix at position pos, ignoring ASCII case?
static bool
matchesNoCase(const string& str,
              size_t pos,
              const char* prefix)
{
    for (; *prefix; ++prefix, ++pos) {
        if ( ( pos >= str.size() ) || ( asciiToLower(str[pos]) != *prefix ) ) {
            return false;
        }
    }

    return true;
}

/*
   %v matches "l", "r" or "view<N>", %V matches "left", "right" or "view<N>".
   The match ignores case, because generateFileNameFromPattern() writes
   "L"/"R" for %v and the view name itself (e.g. "Left") for %V.
 */
static bool
matchesView(bool longView,
            const string& filename,
            size_t startingPos,
            size_t *endPos,
            int* viewNumber)
{
    const char* names[2] = { "l", "r" };
    if (longView) {
        names[0] = "left";
        names[1] = "right";
    }
    for (int v = 0; v < 2; ++v) {
        if ( matchesNoCase(filename, startingPos, names[v]) ) {
            *viewNumber = v;
            *endPos = startingPos + std::char_traits<char>::length(names[v]);

            return true;
        }
    }
    if ( matchesNoCase(filename, startingPos, "view") ) {
        string viewNoStr;
        for (size_t it = startingPos + 4; it < filename.size() && isAsciiDigit(filename[it]); ++it) {
            viewNoStr.push_back(filename[it]);
        }
        if ( stringToInt(viewNoStr, viewNumber) ) {
            *endPos = startingPos + 4 + viewNoStr.size();

            return true;
        }
    }

    return false;
} // matchesView

static bool
matchesPattern_v2(const string& filename,
                  const string& pattern,
                  const string& patternExtension,
                  int* frameNumber,
                  int* viewNumber)
{
    ///If the frame number is found twice or more, this is to verify if they are identical
    bool wasFrameNumberSet = false;

    ///If the view number is found twice or more, this is to verify if they are identical
    bool wasViewNumberSet = false;

    ///Default view number and frame number
    assert(viewNumber && frameNumber);
    *viewNumber = 0;
    *frameNumber = -1;

    ///Iterators on the pattern and the filename
    size_t filenameIt = 0;
    size_t patternIt = 0;

    ///make a copy of the filename from which we remove the file extension
    string filenameCpy = filename;
    string fileExt = removeFileExtension(filenameCpy);

    ///Extensions not matching, exit.
    if (fileExt != patternExtension) {     //
        return false;
    }

    ///Iterating while not at end of either the pattern or the filename
    while ( filenameIt < filenameCpy.size() && patternIt < pattern.size() ) {
        ///the count of '#' characters found
        int sharpCount = 0;

        ///Actually start counting the #
        string variable;
        for (size_t sharpIt = patternIt;
             sharpIt < pattern.size() && pattern[sharpIt] == '#';
             ++sharpIt) {
            ++sharpCount;
            variable.push_back('#');
        }

        ///Did we found a %d style syntax ?
        bool foundPrintFLikeSyntax = false;

        ///Did we found a %v style syntax ?
        bool foundShortView = false;

        ///Did we found a %V style syntax ?
        bool foundLongView = false;

        ///How many digits the printf style %d syntax are desired, e.g %04d is 4
        int printfDigitCount = 0;

        ///The number of characters that compose the %04d style variable, this is at least 2 (%d)
        int printfLikeVariableSize = 2;
        if (pattern[patternIt] == '%') {
            ///We found the '%' digit, start at the character right after to
            ///find digits
            size_t printfIt;
            string digitStr;
            for (printfIt = patternIt + 1;
                 printfIt < pattern.size() && isAsciiDigit(pattern[printfIt]);
                 ++printfIt) {
                digitStr.push_back(pattern[printfIt]);
                ++printfLikeVariableSize;
            }

            ///they are no more digit after the '%', check if this is correctly terminating by a 'd' character.
            /// We also treat the view %v and %V cases here
            if ( ( printfIt < pattern.size() ) && ( asciiToLower(pattern[printfIt]) == 'd') ) {
                foundPrintFLikeSyntax = true;
                if ( !digitStr.empty() && !stringToInt(digitStr, &printfDigitCount) ) {
                    return false;
                }
            } else if ( ( printfIt < pattern.size() ) && ( pattern[printfIt] == 'V') ) {
                foundLongView = true;
            } else if ( ( printfIt < pattern.size() ) && ( pattern[printfIt] == 'v') ) {
                foundShortView = true;
            }
        }


        if (sharpCount > 0) {     ///If we found #
            ///There cannot be another variable!
            assert(!foundPrintFLikeSyntax && !foundLongView && !foundShortView);
            size_t endHashTag = 0;
            int fNumber = -1;

            ///check if the filename matches the number of hashes
            if ( !matchesFrameNumber(sharpCount, filenameCpy, filenameIt, &endHashTag, &fNumber) ) {
                return false;
            }

            ///If the frame number had already been set and it was different, this filename doesn't match
            ///the pattern.
            if ( wasFrameNumberSet && ( fNumber != *frameNumber) ) {
                return false;
            }

            wasFrameNumberSet = true;
            *frameNumber = fNumber;

            ///increment iterators to after the variable
            filenameIt = endHashTag;
            patternIt += sharpCount;
        } else if (foundPrintFLikeSyntax) {     ///If we found a %d style syntax
            ///There cannot be another variable!
            assert(sharpCount == 0 && !foundLongView && !foundShortView);

            size_t endPrintfLike = 0;
            int fNumber = -1;
            ///check if the filename matches the %d syntax
            if ( !matchesFrameNumber(printfDigitCount, filenameCpy, filenameIt, &endPrintfLike, &fNumber) ) {
                return false;
            }

            ///If the frame number had already been set and it was different, this filename doesn't match
            ///the pattern.
            if ( wasFrameNumberSet && ( fNumber != *frameNumber) ) {
                return false;
            }

            wasFrameNumberSet = true;
            *frameNumber = fNumber;

            ///increment iterators to after the variable
            filenameIt = endPrintfLike;
            patternIt += printfLikeVariableSize;
        } else if (foundLongView) {
            ///There cannot be another variable!
            assert(sharpCount == 0 && !foundPrintFLikeSyntax && !foundShortView);

            size_t endVar;
            int vNumber;
            ///check if the filename matches the %V syntax
            if ( !matchesView(true, filenameCpy, filenameIt, &endVar, &vNumber) ) {
                return false;
            }

            ///If the view number had already been set and it was different, this filename doesn't match
            ///the pattern.
            if ( wasViewNumberSet && ( vNumber != *viewNumber) ) {
                return false;
            }

            wasViewNumberSet = true;
            *viewNumber = vNumber;

            ///increment iterators to after the variable
            filenameIt = endVar;
            patternIt += 2;
        } else if (foundShortView) {
            ///There cannot be another variable!
            assert(sharpCount == 0 && !foundPrintFLikeSyntax && !foundLongView);

            size_t endVar;
            int vNumber;
            ///check if the filename matches the %v syntax
            if ( !matchesView(false, filenameCpy, filenameIt, &endVar, &vNumber) ) {
                return false;
            }

            ///If the view number had already been set and it was different, this filename doesn't match
            ///the pattern.
            if ( wasViewNumberSet && ( vNumber != *viewNumber) ) {
                return false;
            }

            wasViewNumberSet = true;
            *viewNumber = vNumber;

            ///increment iterators to after the variable
            filenameIt = endVar;
            patternIt += 2;
        } else {
            ///we found nothing, just compare the characters
            if (pattern[patternIt] != filenameCpy[filenameIt]) {
                return false;
            }
            ++patternIt;
            ++filenameIt;
        }
    }

    bool fileNameAtEnd =  filenameIt >= filenameCpy.size();
    bool patternAtEnd =  patternIt >= pattern.size();
    if (!fileNameAtEnd || !patternAtEnd) {
        return false;
    }

    return true;
} // matchesPattern_v2

/*
   Can the number strings a and b be written with the same padding N (### or %0Nd)?
   A number string that starts with '0' (including "0" itself) can only be written
   with a padding equal to its length; a number string that does not start with '0'
   can be written with any padding from 1 to its length. For example "0" and "10" are
   both written with "#", "08" and "10" with "##", but "08" and "9" can't be in the same sequence.
 */
static bool
samePaddingPossible(const string& a,
                    const string& b)
{
    if ( a.empty() || b.empty() ) {
        return false;
    }
    bool aFixed = ( a[0] == '0' );
    bool bFixed = ( b[0] == '0' );
    if (aFixed && bFixed) {
        return a.size() == b.size();
    } else if (aFixed) {
        return b.size() >= a.size();
    } else if (bFixed) {
        return a.size() >= b.size();
    }

    return true;
}

static int countLeadingZeroes(const string& str)
{
    int ret = 0;

    std::size_t i = 0;
    while ( i < str.size() ) {
        if (str[i] == '0') {
            ++ret;
        } else {
            break;
        }
        ++i;
    }

    return ret;
}

} // namespace {


namespace SequenceParsing {
/**
 * @brief A small structure representing an element of a file name.
 * It can be either a text part, or a view part or a frame number part.
 **/
struct FileNameElement
{
    enum Type { TEXT = 0, FRAME_NUMBER };

    FileNameElement(const string& data,
                    FileNameElement::Type type)
        : data(data)
        , type(type)
    {}

    string data;
    Type type;
};


////////////////////FileNameContent//////////////////////////
struct FileNameContentPrivate
{
    ///Ordered from left to right, these are the elements composing the filename without its path
    vector<FileNameElement> orderedElements;
    string absoluteFileName;
    string filePath;     //!< the filepath
    string filename;     //!< the filename without path
    string extension;     //!< the file extension
    int leadingZeroes; //!< leading zeroes for the last number seen in the file path??? why store this?

    FileNameContentPrivate()
        : orderedElements()
        , absoluteFileName()
        , filePath()
        , filename()
        , extension()
        , leadingZeroes(0)
    {
    }
};


FileNameContent::FileNameContent(const string& absoluteFilename)
    : _imp( new FileNameContentPrivate() )

{
    _imp->absoluteFileName = absoluteFilename;
    _imp->filename = absoluteFilename;
    _imp->filePath = removePath(_imp->filename);
    // Numbers in the extension (IMG_0001.CR2, .jp2, .mp4, .3fr) are not frame numbers:
    // they are part of the text after the last '.'.
    const size_t extensionDotPos = _imp->filename.find_last_of('.');
    string lastNumberStr;
    string lastTextPart;
    for (size_t i = 0; i < _imp->filename.size(); ++i) {
        const char& c = _imp->filename[i];
        if ( isAsciiDigit(c) && ( ( extensionDotPos == string::npos) || (i < extensionDotPos) ) ) {
            lastNumberStr += c;
            if ( !lastTextPart.empty() ) {
                _imp->orderedElements.push_back( FileNameElement(lastTextPart, FileNameElement::TEXT) );
                lastTextPart.clear();
            }
        } else {
            if ( !lastNumberStr.empty() ) {
                _imp->orderedElements.push_back( FileNameElement(lastNumberStr, FileNameElement::FRAME_NUMBER) );
                _imp->leadingZeroes = countLeadingZeroes(lastNumberStr);     //< take into account only the last FRAME_NUMBER
                lastNumberStr.clear();
            }

            lastTextPart.push_back(c);
        }
    }

    if ( !lastNumberStr.empty() ) {
        _imp->orderedElements.push_back( FileNameElement(lastNumberStr, FileNameElement::FRAME_NUMBER) );
        _imp->leadingZeroes = countLeadingZeroes(lastNumberStr);     //< take into account only the last FRAME_NUMBER
        lastNumberStr.clear();
    }
    if ( !lastTextPart.empty() ) {
        _imp->orderedElements.push_back( FileNameElement(lastTextPart, FileNameElement::TEXT) );
        lastTextPart.clear();
    }

    // extension is everything after the last '.'
    size_t lastDotPos = _imp->filename.find_last_of('.');
    if (lastDotPos == string::npos) {
        _imp->extension.clear();
    } else {
        _imp->extension = _imp->filename.substr(lastDotPos + 1);
    }
}

FileNameContent::FileNameContent(const FileNameContent& other)
    : _imp( new FileNameContentPrivate() )
{
    *this = other;
}

FileNameContent::~FileNameContent()
{
}

void
FileNameContent::operator=(const FileNameContent& other)
{
    _imp->orderedElements = other._imp->orderedElements;
    _imp->absoluteFileName = other._imp->absoluteFileName;
    _imp->filename = other._imp->filename;
    _imp->filePath = other._imp->filePath;
    _imp->extension = other._imp->extension;
    _imp->leadingZeroes = other._imp->leadingZeroes;
}

int
FileNameContent::getLeadingZeroes() const
{
    return _imp->leadingZeroes;
}

/**
 * @brief Returns the file path, e.g: /Users/Lala/Pictures/ with the trailing separator.
 **/
const string&
FileNameContent::getPath() const
{
    return _imp->filePath;
}

/**
 * @brief Returns the filename without its path.
 **/
const string&
FileNameContent::fileName() const
{
    return _imp->filename;
}

/**
 * @brief Returns the absolute filename as it was given in the constructor arguments.
 **/
const string&
FileNameContent::absoluteFileName() const
{
    return _imp->absoluteFileName;
}

const string&
FileNameContent::getExtension() const
{
    return _imp->extension;
}

/**
 * @brief Returns the file pattern found in the filename with hash characters style for frame number (i.e: ###)
 **/
string
FileNameContent::getFilePattern(int numHashes) const
{
    // Not cached: the pattern depends on numHashes, and a const method that modifies
    // the object would not be thread-safe.
    string pattern;
    int numberIndex = 0;

    for (size_t j = 0; j < _imp->orderedElements.size(); ++j) {
        const FileNameElement& e = _imp->orderedElements[j];
        switch (e.type) {
        case FileNameElement::TEXT:
            pattern.append(e.data);
            break;
        case FileNameElement::FRAME_NUMBER:
            pattern.append(numHashes, '#');
            pattern.append( stringFromInt(numberIndex) );
            ++numberIndex;
            break;
        default:
            break;
        }
    }

    return pattern;
}

/**
 * @brief If the filename is composed of several numbers (e.g: file08_001.png),
 * this functions returns the number at index as a string that will be stored in numberString.
 * If Index is greater than the number of numbers in the filename or if this filename doesn't
 * contain any number, this function returns false.
 **/
bool
FileNameContent::getNumberByIndex(int index,
                                  string* numberString) const
{
    int numbersElementsIndex = 0;

    for (size_t i = 0; i < _imp->orderedElements.size(); ++i) {
        if (_imp->orderedElements[i].type == FileNameElement::FRAME_NUMBER) {
            if (numbersElementsIndex == index) {
                *numberString = _imp->orderedElements[i].data;

                return true;
            }
            ++numbersElementsIndex;
        }
    }

    return false;
}

int
FileNameContent::getPotentialFrameNumbersCount() const
{
    int count = 0;

    for (size_t i = 0; i < _imp->orderedElements.size(); ++i) {
        if (_imp->orderedElements[i].type == FileNameElement::FRAME_NUMBER) {
            ++count;
        }
    }

    return count;
}

/**
 * @brief Given the pattern of this file, it tries to match the other file name to this
 * pattern.
 * @param numberIndexToVary [out] In case the pattern contains several numbers (@see getNumberByIndex)
 * this value will be fed the appropriate number index that should be used for frame number.
 * For example, if this filename is myfile001_000.jpg and the other file is myfile001_001.jpg
 * numberIndexToVary would be 1 as the frame number string identified in that case is the last number.
 * @returns True if it identified 'other' as belonging to the same sequence, false otherwise.
 **/
bool
FileNameContent::matchesPattern(const FileNameContent& other,
                                int* numberIndexToVary) const
{
    const vector<FileNameElement>& otherElements = other._imp->orderedElements;

    if ( otherElements.size() != _imp->orderedElements.size() ) {
        return false;
    }

    ///We only consider the last potential frame number
    ///
    *numberIndexToVary = -1;

    int numbersCount = 0;
    for (size_t i = 0; i < _imp->orderedElements.size(); ++i) {
        if (_imp->orderedElements[i].type != otherElements[i].type) {
            return false;
        }
        if (_imp->orderedElements[i].type == FileNameElement::FRAME_NUMBER) {
            // a varying number must be written with the same padding in both names
            const string& thisNumber = _imp->orderedElements[i].data;
            const string& otherNumber = otherElements[i].data;
            if (thisNumber == otherNumber) {
                ++numbersCount;
                continue;
            }
            // a number that differs but can't be written with the same padding
            // (e.g. "8" and "08") means another sequence
            if ( !samePaddingPossible(thisNumber, otherNumber) ) {
                *numberIndexToVary = -1;

                return false;
            }
            if (*numberIndexToVary == -1) {
                *numberIndexToVary = numbersCount;
            } else {
                // cannot have more than one varying number, eg:
                // Digital_LAD_2048x1556.cin
                // Digital_LAD_4096x3112.cin
                *numberIndexToVary = -1;

                return false;
            }

            ++numbersCount;
        } else if ( ( _imp->orderedElements[i].type == FileNameElement::TEXT) && ( _imp->orderedElements[i].data != otherElements[i].data) ) {
            return false;
        }
    }
    ///strings are identical
    /// we only accept files with 1 varying number
    if (*numberIndexToVary == -1) {
        return false;
    }

    /*Code commented-out : In this previous version we would try to find several places in the filename where the
       frame number would vary, such as:
       mySequence###lalala###.jpg  This led to very complicated cases to handle for too much troubles.
       The new version only assumes the frame number index varying is the last number varying.
     */

    //    ///find out in the potentialFrameNumbers what is the minimum with pairs and pick it up
    //    /// for example if 1 pair is : < 0001, 802398 > and the other pair is < 01 , 10 > we pick
    //    /// the second one.
    //    vector<int> minIndexes;
    //    int minimum = INT_MAX;
    //    for (size_t i = 0; i < potentialFrameNumbers.size(); ++i) {
    //        int thisNumber = stringToInt(potentialFrameNumbers[i].second.first);
    //        int otherNumber = stringToInt(potentialFrameNumbers[i].second.second);
    //        int diff = std::abs(thisNumber - otherNumber);
    //        if (diff < minimum) {
    //            minimum = diff;
    //            minIndexes.clear();
    //            minIndexes.push_back(i);
    //        } else if (diff == minimum) {
    //            minIndexes.push_back(i);
    //        }
    //    }
    //    for (size_t i = 0; i < minIndexes.size(); ++i) {
    //        numberIndexesToVary->push_back(potentialFrameNumbers[minIndexes[i]].first);

    //    }
    return true;
} // FileNameContent::matchesPattern

void
FileNameContent::generatePatternWithFrameNumberAtIndex(int index,
                                                       int numHashes,
                                                       string* pattern) const
{
    // Build the pattern from the elements: the number at index becomes numHashes '#',
    // the other numbers are kept as they are.
    string unpathedPattern;
    int numbersCount = 0;

    for (size_t i = 0; i < _imp->orderedElements.size(); ++i) {
        const FileNameElement& e = _imp->orderedElements[i];
        if ( (e.type == FileNameElement::FRAME_NUMBER) && (numbersCount++ == index) ) {
            unpathedPattern.append(numHashes, '#');
        } else {
            unpathedPattern.append(e.data);
        }
    }

    *pattern = getPath() + unpathedPattern;
}

string
removePath(string& filename)
{
    ///find the last separator
    size_t pos = filename.find_last_of('/');

    if (pos == string::npos) {
        //try out \\ char
        pos = filename.find_last_of('\\');
    }
    if (pos == string::npos) {
        ///couldn't find a path
        return "";
    }
    string path = filename.substr(0, pos + 1);  // + 1 to include the trailing separator
    removeAllOccurences(filename, path);

    return path;
}

bool
filesListFromPattern_fast(const string& pattern,
                          const StringList &files,
                          SequenceParsing::SequenceFromPattern* sequence)
{
    if ( pattern.empty() ) {
        return false;
    }
    string patternUnPathed = pattern;
    string patternPath = removePath(patternUnPathed);
    string patternExtension = removeFileExtension(patternUnPathed);

    for (size_t i = 0; i < files.size(); ++i) {
        int frameNumber;
        int viewNumber;
        if ( matchesPattern_v2(files[i], patternUnPathed, patternExtension, &frameNumber, &viewNumber) ) {
            SequenceFromPattern::iterator it = sequence->find(frameNumber);
            string absoluteFileName = patternPath + files[i];
            if ( it != sequence->end() ) {
                pair<map<int, string>::iterator, bool> ret =
                    it->second.insert( make_pair(viewNumber, absoluteFileName) );
                if (!ret.second) {
#                 ifdef DEBUG
                    std::cerr << "There was an issue populating the file sequence. Several files with the same frame number"
                        " have the same view index." << std::endl;
#                 endif
                }
            } else {
                map<int, string> viewsMap;
                viewsMap.insert( make_pair(viewNumber, absoluteFileName) );
                sequence->insert( make_pair(frameNumber, viewsMap) );
            }
        }
    }

    return true;
}

bool
filesListFromPattern_slow(const string& pattern,
                          SequenceParsing::SequenceFromPattern* sequence)
{
    if ( pattern.empty() ) {
        return false;
    }

    string patternUnPathed = pattern;
    tinydir_dir patternDir;

#if (defined(_WIN32) || defined(__WIN32__) || defined(WIN32)) && defined(UNICODE)
    wstring patternPath = utf8_to_utf16(removePath(patternUnPathed));
#else
    string patternPath = removePath(patternUnPathed);
#endif
    if (tinydir_open( &patternDir, patternPath.c_str() ) == -1) {
        return false;
    }

    ///all the interesting files of the pattern directory
    StringList files;
    getFilesFromDir(patternDir, &files);
    tinydir_close(&patternDir);

    return filesListFromPattern_fast(pattern, files, sequence);
}

StringList
sequenceFromPatternToFilesList(const SequenceParsing::SequenceFromPattern& sequence,
                               int onlyViewIndex)
{
    StringList ret;

    for (SequenceParsing::SequenceFromPattern::const_iterator it = sequence.begin(); it != sequence.end(); ++it) {
        const map<int, string>& views = it->second;

        for (map<int, string>::const_iterator it2 = views.begin(); it2 != views.end(); ++it2) {
            if ( ( onlyViewIndex != -1) && ( it2->first != onlyViewIndex) && ( it2->first != -1) ) {
                continue;
            }
            ret.push_back(it2->second);
        }
    }

    return ret;
}

string
generateFileNameFromPattern(const string& pattern,
                            const vector<string>& viewNames,
                            int frameNumber,
                            int viewNumber)
{
    string patternUnPathed = pattern;
    string patternPath = removePath(patternUnPathed);
    string patternExtension = removeFileExtension(patternUnPathed);

    ///the pattern has no extension, switch the extension and the unpathed part
    if ( patternUnPathed.empty() ) {
        patternUnPathed = patternExtension;
        patternExtension.clear();
    }
    ///this list represents the common parts of the filename to find in a file in order for it to match the pattern.
    StringList commonPartsToFind;
    ///this list represents the variables ( ###  %04d %v etc...) found in the pattern ordered from left to right in the
    ///original string.
    vector<pair<string, int> > variablesByOrder;
    extractCommonPartsAndVariablesFromPattern(pattern, patternExtension, &commonPartsToFind, &variablesByOrder);

    string output = pattern;
    size_t lastVariablePos = string::npos;
    for (size_t i = 0; i < variablesByOrder.size(); ++i) {
        const string& variable = variablesByOrder[i].first;
        lastVariablePos = findStr(output, variable, lastVariablePos != string::npos ? lastVariablePos : 0);

        ///if we can't find the variable that means extractCommonPartsAndVariablesFromPattern is bugged.
        assert(lastVariablePos != string::npos);

        if (variable.find_first_of('#') != string::npos) {
            output.replace( lastVariablePos, variable.size(), frameNumberToString( frameNumber, (int)variable.size() ) );
        } else if (variable.find("%v") != string::npos) {
            string viewNumberStr;
            if ( ( viewNumber >= 0) && ( viewNumber < (int)viewNames.size() ) ) {
                viewNumberStr = firstCharacterUpper(viewNames[viewNumber]);
            }


            output.replace(lastVariablePos, variable.size(), viewNumberStr);
        } else if (variable.find("%V") != string::npos) {
            if ( ( viewNumber >= 0) && ( viewNumber < (int)viewNames.size() ) ) {
                const string& viewNumberStr = viewNames[viewNumber];
                output.replace(lastVariablePos, variable.size(), viewNumberStr);
            }
        } else if ( startsWith(variable, "%0") && endsWith(variable, "d") ) {
            string digitsCountStr = variable;
            removeAllOccurences(digitsCountStr, "%0");
            removeAllOccurences(digitsCountStr, "d");
            int digitsCount = 0;
            if ( !digitsCountStr.empty() && !stringToInt(digitsCountStr, &digitsCount) ) {
                throw std::invalid_argument("Unrecognized pattern: " + pattern);
            }
            output.replace( lastVariablePos, variable.size(), frameNumberToString(frameNumber, digitsCount) );
        } else if (variable == "%d") {
            output.replace( lastVariablePos, variable.size(), frameNumberToString(frameNumber, 1) );
        } else {
            throw std::invalid_argument("Unrecognized pattern: " + pattern);
        }
    }

    return output;
} // generateFileNameFromPattern

struct SequenceFromFilesPrivate
{
    ///all the files mapped to their index
    map<int, FileNameContent> filesMap;

    /// The index of the frame number string in case there're several numbers in a filename.
    int frameNumberStringIndex;
    unsigned long long totalSize;
    bool sizeEstimationEnabled;

    SequenceFromFilesPrivate(bool enableSizeEstimation)

        : filesMap()
        , frameNumberStringIndex(-1)
        , totalSize(0)
        , sizeEstimationEnabled(enableSizeEstimation)
    {
    }

    bool isInSequence(int index) const
    {
        return filesMap.find(index) != filesMap.end();
    }

    /// The number of hash tags # for the pattern: the length of the frame numbers that start
    /// with '0', if any, else the length of the shortest frame number.
    int getNumHashes() const
    {
        int numHashes = INT_MAX;
        for (map<int, FileNameContent>::const_iterator it = filesMap.begin(); it != filesMap.end(); ++it) {
            string frameNumberStr;
            if ( it->second.getNumberByIndex(frameNumberStringIndex, &frameNumberStr) && !frameNumberStr.empty() ) {
                if (frameNumberStr[0] == '0') {
                    return (int)frameNumberStr.size();
                }
                numHashes = std::min( numHashes, (int)frameNumberStr.size() );
            }
        }

        return numHashes == INT_MAX ? 0 : numHashes;
    }
};

SequenceFromFiles::SequenceFromFiles(bool enableSizeEstimation)
    : _imp( new SequenceFromFilesPrivate(enableSizeEstimation) )
{
}

SequenceFromFiles::SequenceFromFiles(const FileNameContent& firstFile,
                                     bool enableSizeEstimation)
    : _imp( new SequenceFromFilesPrivate(enableSizeEstimation) )
{
    tryInsertFile(firstFile);
}

SequenceFromFiles::~SequenceFromFiles()
{
}

SequenceFromFiles::SequenceFromFiles(const SequenceFromFiles& other)
    : _imp( new SequenceFromFilesPrivate(false) )
{
    *this = other;
}

void
SequenceFromFiles::operator=(const SequenceFromFiles& other) const
{
    _imp->filesMap = other._imp->filesMap;
    _imp->frameNumberStringIndex = other._imp->frameNumberStringIndex;
    _imp->totalSize = other._imp->totalSize;
    _imp->sizeEstimationEnabled = other._imp->sizeEstimationEnabled;
}

bool
SequenceFromFiles::tryInsertFile(const FileNameContent& file,
                                 bool checkPath)
{
    if ( _imp->filesMap.empty() ) {
        ///Special case when the sequence is empty, we don't have anything to match against.
        string frameNumberStr;
        _imp->frameNumberStringIndex = file.getPotentialFrameNumbersCount() - 1;
        int frameNumber = -1;
        bool ok = file.getNumberByIndex(_imp->frameNumberStringIndex, &frameNumberStr);
        if ( ok && !stringToInt(frameNumberStr, &frameNumber) ) {
            // the number is too large to be a frame number: this file has no frame number,
            // and no other file can join it
            frameNumber = -1;
            _imp->frameNumberStringIndex = -1;
        }
        _imp->filesMap.insert( make_pair(frameNumber, file) );

        if (_imp->sizeEstimationEnabled) {
            _imp->totalSize += getFileSize( file.absoluteFileName() );
        }

        return true;
    }

    assert( !_imp->filesMap.empty() );

    const FileNameContent& firstFileContent = _imp->filesMap.begin()->second;


    if ( checkPath && ( file.getPath() != firstFileContent.getPath() ) ) {
        return false;
    }

    ///If the filename has several numbers, such as toto11_titi22.jpg
    ///This corresponds to the index of the number in which the frame number is expected to be.
    ///e.g in this example if 22 would be the frame number, frameNumberIndex would be 1.
    int frameNumberIndex;
    bool insert = false;
    if ( file.matchesPattern(firstFileContent, &frameNumberIndex) ) {
        if (frameNumberIndex == _imp->frameNumberStringIndex) {
            insert = true;

            string frameNumberStr;
            int frameNumber = -1;
            bool ok = file.getNumberByIndex(_imp->frameNumberStringIndex, &frameNumberStr) &&
                      stringToInt(frameNumberStr, &frameNumber);
            if (ok) {
                pair<map<int, FileNameContent>::iterator, bool> success =
                    _imp->filesMap.insert( make_pair(frameNumber, file) );

                ///Insert might have failed because we didn't check prior to this whether the file was already
                ///present or not.
                if (success.second) {
                    if (_imp->sizeEstimationEnabled) {
                        _imp->totalSize += getFileSize( file.absoluteFileName() );
                    }
                } else {
                    return false;
                }
            } else {
                return false;
            }
        }
    }

    return insert;
} // SequenceFromFiles::tryInsertFile

bool
SequenceFromFiles::contains(const string& absoluteFileName) const
{
    for (map <int, FileNameContent >::const_iterator it = _imp->filesMap.begin(); it != _imp->filesMap.end(); ++it) {
        if (it->second.absoluteFileName() == absoluteFileName) {
            return true;
        }
    }

    return false;
}

bool
SequenceFromFiles::empty() const
{
    return _imp->filesMap.empty();
}

int
SequenceFromFiles::count() const
{
    return (int)_imp->filesMap.size();
}

bool
SequenceFromFiles::isSingleFile() const
{
    return _imp->filesMap.size() == 1;
}

int
SequenceFromFiles::getFirstFrame() const
{
    if ( _imp->filesMap.empty() ) {
        return INT_MIN;
    } else {
        return _imp->filesMap.begin()->first;
    }
}

int
SequenceFromFiles::getLastFrame() const
{
    if ( _imp->filesMap.empty() ) {
        return INT_MAX;
    } else {
        map<int, FileNameContent>::const_iterator it = _imp->filesMap.end();
        --it;

        return it->first;
    }
}

const map<int, FileNameContent>&
SequenceFromFiles::getFrameIndexes() const
{
    return _imp->filesMap;
}

unsigned long long
SequenceFromFiles::getEstimatedTotalSize() const
{
    return _imp->totalSize;
}

string
SequenceFromFiles::generateValidSequencePattern() const
{
    if ( empty() ) {
        return "";
    }
    if ( isSingleFile() ) {
        return _imp->filesMap.begin()->second.absoluteFileName();
    }
    assert(_imp->filesMap.size() >= 2);
    string firstFramePattern;
    _imp->filesMap.begin()->second.generatePatternWithFrameNumberAtIndex(_imp->frameNumberStringIndex,
                                                                         _imp->getNumHashes(),
                                                                         &firstFramePattern);

    return firstFramePattern;
}

string
SequenceFromFiles::generateUserFriendlySequencePatternFromValidPattern(const string& validPattern) const
{
    assert( !isSingleFile() );
    string pattern = validPattern;
    vector< pair<int, int> > chunks;
    //int first = getFirstFrame();
    map<int, FileNameContent>::const_iterator first = _imp->filesMap.begin();
    map<int, FileNameContent>::const_iterator cur = first;
    map<int, FileNameContent>::const_iterator next = first;
    if ( next != _imp->filesMap.end() ) {
        ++next;
    }
    while ( first != _imp->filesMap.end() ) {
        int breakCounter = 0;
        while ( next != _imp->filesMap.end() && cur != _imp->filesMap.end() && next->first == (cur->first + 1) ) {
            if ( next != _imp->filesMap.end() ) {
                ++next;
            }
            if ( cur != _imp->filesMap.end() ) {
                ++cur;
            }
            ++breakCounter;
        }
        if ( cur != _imp->filesMap.end() ) {
            chunks.push_back( make_pair(first->first, cur->first) );
        }
        first = next;
        if ( next != _imp->filesMap.end() ) {
            ++next;
        }
        cur = first;
    }

    if (chunks.size() == 1) {
        pattern += ' ';
        pattern += stringFromInt(chunks[0].first);
        pattern += '-';
        pattern += stringFromInt(chunks[0].second);
    } else {
        pattern.append(" ( ");
        for (size_t i = 0; i < chunks.size(); ++i) {
            if (chunks[i].first != chunks[i].second) {
                pattern += ' ';
                pattern += stringFromInt(chunks[i].first);
                pattern += '-';
                pattern += stringFromInt(chunks[i].second);
            } else {
                pattern += ' ';
                pattern += stringFromInt(chunks[i].first);
            }
            if (i < chunks.size() - 1) {
                pattern.append(" /");
            }
        }
        pattern.append(" ) ");
    }

    return pattern;
} // SequenceFromFiles::generateUserFriendlySequencePatternFromValidPattern

string
SequenceFromFiles::generateUserFriendlySequencePattern() const
{
    if ( isSingleFile() ) {
        return _imp->filesMap.begin()->second.fileName();
    }
    string pattern = generateValidSequencePattern();
    removePath(pattern);

    return generateUserFriendlySequencePatternFromValidPattern(pattern);
}

string
SequenceFromFiles::fileExtension() const
{
    if ( !empty() ) {
        return _imp->filesMap.begin()->second.getExtension();
    } else {
        return string();
    }
}

string
SequenceFromFiles::getPath() const
{
    if ( !empty() ) {
        return _imp->filesMap.begin()->second.getPath();
    } else {
        return string();
    }
}
} // namespace SequenceParsing

