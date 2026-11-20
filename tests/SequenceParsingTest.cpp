// Standalone tests for SequenceParsing.
// Build with tests/CMakeLists.txt; the program returns 0 if all checks pass.

#include "SequenceParsing.h"

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <locale>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <direct.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace SequenceParsing;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond) \
    do { \
        ++g_checks; \
        if ( !(cond) ) { \
            ++g_failures; \
            std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK failed: " #cond << std::endl; \
        } \
    } while (0)

#define CHECK_EQ(expected, actual) \
    do { \
        ++g_checks; \
        if ( !( (expected) == (actual) ) ) { \
            ++g_failures; \
            std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK_EQ failed: " #expected " == " #actual \
                      << "\n    expected: " << (expected) << "\n    actual:   " << (actual) << std::endl; \
        } \
    } while (0)

static SequenceFromPattern
parse(const std::string& pattern,
      const StringList& files)
{
    SequenceFromPattern seq;

    filesListFromPattern_fast(pattern, files, &seq);

    return seq;
}

static StringList
numbered(const std::string& prefix,
         int first,
         int last,
         int padding,
         const std::string& suffix)
{
    StringList ret;

    for (int i = first; i <= last; ++i) {
        std::string n = std::to_string(i);
        while ( (int)n.size() < padding ) {
            n.insert(0, 1, '0');
        }
        ret.push_back(prefix + n + suffix);
    }

    return ret;
}

// Build a SequenceFromFiles by inserting the files in the given order.
static int
insertAll(SequenceFromFiles& seq,
          const StringList& files)
{
    int inserted = 0;

    for (size_t i = 0; i < files.size(); ++i) {
        if ( seq.tryInsertFile( FileNameContent(files[i]) ) ) {
            ++inserted;
        }
    }

    return inserted;
}

static void
testPatterns()
{
    // hashes: at least as many digits, no extra padding
    StringList files = numbered("test_", 0, 10, 0, ".exr");
    CHECK_EQ( 11, (int)parse("test_#.exr", files).size() );
    CHECK_EQ( 1, (int)parse("test_##.exr", files).size() );
    files = numbered("test_", 0, 10, 2, ".exr");
    CHECK_EQ( 11, (int)parse("test_##.exr", files).size() );
    CHECK_EQ( 1, (int)parse("test_#.exr", files).size() ); // only test_10.exr

    // printf-like
    files = numbered("test_", 0, 10, 4, ".exr");
    CHECK_EQ( 11, (int)parse("test_%04d.exr", files).size() );
    CHECK_EQ( 0, (int)parse("test_%02d.exr", files).size() );
    CHECK_EQ( 11, (int)parse("/some/path/test_####.exr", files).size() );
    SequenceFromPattern seq = parse("/some/path/test_####.exr", files);
    CHECK_EQ( std::string("/some/path/test_0003.exr"), seq[3][0] );

    // filenames that are all digits
    files.clear();
    files.push_back("12345.exr");
    files.push_back("93830.exr");
    files.push_back("03829.exr");
    files.push_back("003830.exr");
    CHECK_EQ( 3, (int)parse("#####.exr", files).size() );

    // a pattern without variables matches only itself, with frame -1
    files = numbered("test_", 0, 10, 0, ".exr");
    seq = parse("test_3.exr", files);
    CHECK_EQ( 1, (int)seq.size() );
    CHECK( seq.find(-1) != seq.end() );

    // extensions with digits
    files = numbered("clip.", 1, 5, 4, ".mp4");
    CHECK_EQ( 5, (int)parse("clip.####.mp4", files).size() );

    CHECK_EQ( 0, (int)parse("", files).size() );
}

static void
testViews()
{
    StringList files;

    for (int i = 0; i < 5; ++i) {
        files.push_back("shot_" + std::to_string(i) + ".left.exr");
        files.push_back("shot_" + std::to_string(i) + ".right.exr");
        files.push_back("shot_" + std::to_string(i) + ".l.exr");
        files.push_back("shot_" + std::to_string(i) + ".r.exr");
        files.push_back("shot_" + std::to_string(i) + ".view2.exr");
    }
    SequenceFromPattern seq = parse("shot_#.%V.exr", files);
    CHECK_EQ( 5, (int)seq.size() );
    CHECK_EQ( 3, (int)seq[0].size() ); // left, right, view2
    seq = parse("shot_#.%v.exr", files);
    CHECK_EQ( 5, (int)seq.size() );
    CHECK_EQ( 3, (int)seq[0].size() ); // l, r, view2
    CHECK_EQ( std::string("shot_4.r.exr"), seq[4][1] );
}

static void
testGenerate()
{
    std::vector<std::string> views;

    views.push_back("Left");
    views.push_back("Right");
    CHECK_EQ( std::string("/Users/Test/Left/toto_L0.tif"), generateFileNameFromPattern("/Users/Test/%V/toto_%v#.tif", views, 0, 0) );
    CHECK_EQ( std::string("0120.jpg"), generateFileNameFromPattern("####.jpg", views, 120, 0) );
    CHECK_EQ( std::string("120lalala0120.jpg"), generateFileNameFromPattern("###lalala%04d.jpg", views, 120, 0) );
    CHECK_EQ( std::string("0120Right.jpg"), generateFileNameFromPattern("####%V.jpg", views, 120, 1) );
    CHECK_EQ( std::string("weird.L.test_120.unittest"), generateFileNameFromPattern("weird.%v.test_%01d.unittest", views, 120, 0) );
    CHECK_EQ( std::string("12345.jpg"), generateFileNameFromPattern("###.jpg", views, 12345, 0) );
    CHECK_EQ( std::string("img7.jpg"), generateFileNameFromPattern("img%d.jpg", views, 7, 0) );
    CHECK_EQ( std::string("mysequence10.png"), generateFileNameFromPattern("mysequence10.png", views, 120, 0) );
}

static void
testFileNameContent()
{
    FileNameContent c("/Users/Test/mysequence001.jpg");

    CHECK_EQ( std::string("mysequence001.jpg"), c.fileName() );
    CHECK_EQ( std::string("/Users/Test/"), c.getPath() );
    CHECK_EQ( std::string("jpg"), c.getExtension() );
    CHECK_EQ( std::string("mysequence###0.jpg"), c.getFilePattern(3) );
    CHECK_EQ( 2, c.getLeadingZeroes() );
    std::string n;
    CHECK( c.getNumberByIndex(0, &n) );
    CHECK_EQ( std::string("001"), n );
    CHECK( !c.getNumberByIndex(1, &n) );

    int idx = -2;
    CHECK( c.matchesPattern(FileNameContent("/Users/Test/mysequence002.jpg"), &idx) );
    CHECK_EQ( 0, idx );
    CHECK( !c.matchesPattern(FileNameContent("/Users/Test/mysequence01.jpg"), &idx) );
    CHECK( !c.matchesPattern(FileNameContent("/Users/Test/mysequence001.jpg"), &idx) ); // identical
    // more than one varying number
    CHECK( !FileNameContent("Digital_LAD_2048x1556.cin").matchesPattern(FileNameContent("Digital_LAD_4096x3112.cin"), &idx) );

    std::string pattern;
    FileNameContent c2("/a/shot10_v2.0015.exr");
    c2.generatePatternWithFrameNumberAtIndex(2, 4, &pattern);
    CHECK_EQ( std::string("/a/shot10_v2.####.exr"), pattern );

    CHECK_EQ( std::string(""), FileNameContent("noext").getExtension() );
    CHECK_EQ( std::string("hidden"), FileNameContent("/a/.hidden").getExtension() );
}

static void
testSequenceFromFiles()
{
    SequenceFromFiles seq( FileNameContent("/Users/Test/mysequence000.jpg"), false );

    CHECK_EQ( std::string("/Users/Test/mysequence000.jpg"), seq.generateValidSequencePattern() );
    CHECK_EQ( std::string("mysequence000.jpg"), seq.generateUserFriendlySequencePattern() );
    CHECK( seq.isSingleFile() );
    CHECK_EQ( 10, insertAll( seq, numbered("/Users/Test/mysequence", 0, 10, 3, ".jpg") ) ); // 000 is already there
    CHECK_EQ( 0, seq.getFirstFrame() );
    CHECK_EQ( 10, seq.getLastFrame() );
    CHECK_EQ( 11, seq.count() );
    CHECK_EQ( std::string("/Users/Test/mysequence###.jpg"), seq.generateValidSequencePattern() );
    CHECK_EQ( std::string("mysequence###.jpg 0-10"), seq.generateUserFriendlySequencePattern() );
    CHECK( !seq.tryInsertFile( FileNameContent("/Other/mysequence011.jpg") ) );
    CHECK( seq.contains("/Users/Test/mysequence005.jpg") );

    SequenceFromFiles holes( FileNameContent("a_001.png"), false );
    holes.tryInsertFile( FileNameContent("a_002.png") );
    holes.tryInsertFile( FileNameContent("a_005.png") );
    CHECK_EQ( std::string("a_###.png (  1-2 / 5 ) "), holes.generateUserFriendlySequencePattern() );
}

static void
testUtf8()
{
    // UTF-8 file names: their non-ASCII bytes are negative chars where char is signed,
    // and must not be passed to the <cctype> functions.
    const std::string ete("\xC3\xA9t\xC3\xA9_"); // "été_"
    StringList files = numbered(ete, 1, 3, 4, ".exr");
    files.push_back("img\xC3\xA9.exr");
    files.push_back(ete + "\xC3\xA9.exr");
    CHECK_EQ( 3, (int)parse(ete + "####.exr", files).size() );
    CHECK_EQ( 0, (int)parse("img####.exr", files).size() );
    CHECK_EQ( 0, (int)parse("img%04d.exr", files).size() );
    CHECK_EQ( 0, (int)parse("img%v.exr", files).size() );

    FileNameContent c("/d/" + ete + "0012.exr");
    CHECK_EQ( 1, c.getPotentialFrameNumbersCount() );
    CHECK_EQ( ete + "####0.exr", c.getFilePattern(4) );

    // %v uses the whole first character of the view name, not its first byte
    std::vector<std::string> views;
    views.push_back("\xC3\x89" "cran"); // "Écran"
    views.push_back("right");
    CHECK_EQ( std::string("img_\xC3\x89.exr"), generateFileNameFromPattern("img_%v.exr", views, 1, 0) );
    CHECK_EQ( std::string("img_R.exr"), generateFileNameFromPattern("img_%v.exr", views, 1, 1) );
}

static void
testIntOverflow()
{
    // numbers that don't fit in an int are not frame numbers (std::atoi overflow is undefined,
    // and in practice wrapped 4294967297 to frame 1)
    StringList files;
    files.push_back("t_4294967297.exr");
    files.push_back("t_1.exr");
    files.push_back("t_99999999999999999999.exr");
    SequenceFromPattern seq = parse("t_#.exr", files);
    CHECK_EQ( 1, (int)seq.size() );
    CHECK_EQ( std::string("t_1.exr"), seq[1][0] );
    files.clear();
    files.push_back("t_2147483647.exr"); // INT_MAX is fine
    seq = parse("t_#.exr", files);
    CHECK( seq.find(2147483647) != seq.end() );
    CHECK_EQ( 0, (int)parse("t_%099999999999d.exr", files).size() );

    SequenceFromFiles s( FileNameContent("t_1.exr"), false );
    CHECK( !s.tryInsertFile( FileNameContent("t_4294967298.exr") ) );
    CHECK_EQ( 1, s.getLastFrame() );

    std::vector<std::string> views;
    bool thrown = false;
    try {
        generateFileNameFromPattern("t_%099999999999d.exr", views, 1, 0);
    } catch (const std::invalid_argument&) {
        thrown = true;
    }
    CHECK(thrown);
}

static void
testPrintfNoPadding()
{
    // %d is printf's %d: any number without padding, including 0
    StringList files = numbered("img", 0, 12, 0, ".jpg");
    files.push_back("img.jpg");
    files.push_back("img007.jpg");
    SequenceFromPattern seq = parse("img%d.jpg", files);
    CHECK_EQ( 13, (int)seq.size() );
    CHECK( seq.find(0) != seq.end() );
    CHECK_EQ( std::string("img7.jpg"), seq[7][0] );
    CHECK_EQ( 13, (int)parse("img%0d.jpg", files).size() );
    CHECK_EQ( 13, (int)parse("img%1d.jpg", files).size() );
    files.clear();
    files.push_back("img.jpg");
    CHECK_EQ( 0, (int)parse("img%d.jpg", files).size() );
}

static void
testViewsRoundTrip()
{
    // files written with generateFileNameFromPattern() must be found with the same pattern
    std::vector<std::string> views;
    views.push_back("Left");
    views.push_back("Right");
    const char* patterns[] = { "shot_#.%V.exr", "shot_%v.###.exr", "%V/shot_%v_%04d.exr" };
    for (int p = 0; p < 3; ++p) {
        StringList files;
        for (int f = 1; f <= 3; ++f) {
            for (int v = 0; v < 2; ++v) {
                std::string name = generateFileNameFromPattern(patterns[p], views, f, v);
                removePath(name);
                files.push_back(name);
            }
        }
        std::string pattern(patterns[p]);
        removePath(pattern);
        SequenceFromPattern seq = parse(pattern, files);
        CHECK_EQ( 3, (int)seq.size() );
        CHECK_EQ( 2, (int)seq[1].size() );
        CHECK_EQ( files[1], seq[1][1] );
    }
    StringList files;
    files.push_back("a.LEFT.exr");
    files.push_back("a.View3.exr");
    SequenceFromPattern seq = parse("a.%V.exr", files);
    CHECK_EQ( 1, (int)seq.size() );
    CHECK_EQ( 2, (int)seq[-1].size() );
    CHECK_EQ( std::string("a.View3.exr"), seq[-1][3] );
}

static void
testNegativeFrames()
{
    // negative frames are written and read like printf("%04d") does: "-005"
    std::vector<std::string> views;
    CHECK_EQ( std::string("img.-005.exr"), generateFileNameFromPattern("img.####.exr", views, -5, 0) );
    CHECK_EQ( std::string("img.-005.exr"), generateFileNameFromPattern("img.%04d.exr", views, -5, 0) );
    CHECK_EQ( std::string("img.-12345.exr"), generateFileNameFromPattern("img.####.exr", views, -12345, 0) );
    CHECK_EQ( std::string("img.-5.exr"), generateFileNameFromPattern("img.%d.exr", views, -5, 0) );
    CHECK_EQ( std::string("img.-5.exr"), generateFileNameFromPattern("img.#.exr", views, -5, 0) );
    CHECK_EQ( std::string("img.-2147483648.exr"), generateFileNameFromPattern("img.#.exr", views, INT_MIN, 0) );

    StringList files;
    for (int f = -12; f <= 3; ++f) {
        files.push_back( generateFileNameFromPattern("img.####.exr", views, f, 0) );
    }
    files.push_back("img.-0001.exr"); // too much padding
    files.push_back("img.-000.exr"); // -0
    files.push_back("img.-.exr");
    SequenceFromPattern seq = parse("img.####.exr", files);
    CHECK_EQ( 16, (int)seq.size() );
    CHECK_EQ( std::string("img.-012.exr"), seq[-12][0] );
    CHECK_EQ( std::string("img.0000.exr"), seq[0][0] );
    files.clear();
    for (int f = -12; f <= 3; ++f) {
        files.push_back( generateFileNameFromPattern("img.%d.exr", views, f, 0) );
    }
    files.push_back("img.-05.exr");
    seq = parse("img.%d.exr", files);
    CHECK_EQ( 16, (int)seq.size() );
    CHECK_EQ( std::string("img.-5.exr"), seq[-5][0] );
    // a literal '-' in the pattern is still a separator
    files.clear();
    files.push_back("img-001.exr");
    seq = parse("img-###.exr", files);
    CHECK_EQ( 1, (int)seq.size() );
    CHECK( seq.find(1) != seq.end() );
}

static void
testGlobalLocale()
{
    // the global C++ locale (here with digit grouping) must not change the numbers
    struct Grouping
        : std::numpunct<char>
    {
        char do_thousands_sep() const override { return ','; }
        std::string do_grouping() const override { return "\3"; }
    };
    std::locale previous = std::locale::global( std::locale(std::locale::classic(), new Grouping) );
    std::vector<std::string> views;
    CHECK_EQ( std::string("img.12345.exr"), generateFileNameFromPattern("img.####.exr", views, 12345, 0) );
    CHECK_EQ( std::string("img.12345.exr"), generateFileNameFromPattern("img.%d.exr", views, 12345, 0) );
    SequenceFromFiles seq( FileNameContent("/a/img.1000.exr"), false );
    seq.tryInsertFile( FileNameContent("/a/img.1001.exr") );
    CHECK_EQ( std::string("img.####.exr 1000-1001"), seq.generateUserFriendlySequencePattern() );
    StringList files;
    files.push_back("img.1234.exr");
    CHECK_EQ( 1, (int)parse("img.%04d.exr", files).size() );
    std::locale::global(previous);
}

static void
testUnpaddedWithZero()
{
    // unpadded sequence that contains frame 0 and frames >= 10, inserted in directory
    // (alphabetical) order: test_0, test_1, test_10, test_11, test_2, ...
    StringList files = numbered("/d/test_", 0, 11, 0, ".exr");
    std::sort( files.begin(), files.end() );
    SequenceFromFiles seq(false);
    CHECK_EQ( 12, insertAll(seq, files) );
    CHECK_EQ( 0, seq.getFirstFrame() );
    CHECK_EQ( 11, seq.getLastFrame() );
    int idx = -1;
    CHECK( FileNameContent("test_10.exr").matchesPattern(FileNameContent("test_0.exr"), &idx) );
    CHECK( FileNameContent("test_0.exr").matchesPattern(FileNameContent("test_10.exr"), &idx) );
    CHECK( FileNameContent("test_08.exr").matchesPattern(FileNameContent("test_10.exr"), &idx) );
    CHECK( !FileNameContent("test_08.exr").matchesPattern(FileNameContent("test_9.exr"), &idx) );
    CHECK( !FileNameContent("test_9.exr").matchesPattern(FileNameContent("test_08.exr"), &idx) );
    CHECK( !FileNameContent("test_008.exr").matchesPattern(FileNameContent("test_08.exr"), &idx) );
    // 122938 is what %05d writes for frame 122938, in either order
    CHECK( FileNameContent("122938.jpg").matchesPattern(FileNameContent("00001.jpg"), &idx) );
    CHECK( FileNameContent("00001.jpg").matchesPattern(FileNameContent("122938.jpg"), &idx) );
}

static void
testSequencePadding()
{
    // the pattern must match all the files of the sequence, whatever the insertion order
    // (directory listings are often sorted alphabetically: img10 before img9)
    SequenceFromFiles seq(false);
    CHECK_EQ( 4, insertAll( seq, numbered("/d/img", 9, 12, 0, ".png") ) ); // img9 first: OK
    CHECK_EQ( std::string("/d/img#.png"), seq.generateValidSequencePattern() );
    StringList files = numbered("/d/img", 8, 12, 0, ".png");
    std::sort( files.begin(), files.end() ); // img10 img11 img12 img8 img9
    SequenceFromFiles seq2(false);
    CHECK_EQ( 5, insertAll(seq2, files) );
    CHECK_EQ( std::string("/d/img#.png"), seq2.generateValidSequencePattern() );
    CHECK_EQ( std::string("img#.png 8-12"), seq2.generateUserFriendlySequencePattern() );
    for (size_t i = 0; i < files.size(); ++i) {
        removePath(files[i]);
    }
    CHECK_EQ( 5, (int)parse(seq2.generateValidSequencePattern(), files).size() );

    files = numbered("/d/img", 98, 101, 3, ".png");
    std::reverse( files.begin(), files.end() );
    SequenceFromFiles seq3(false);
    CHECK_EQ( 4, insertAll(seq3, files) );
    CHECK_EQ( std::string("/d/img###.png"), seq3.generateValidSequencePattern() );
}

static void
testCopies()
{
    // copies must be complete
    FileNameContent c("/d/img_0042.exr");
    FileNameContent copy(c);
    CHECK_EQ( 2, c.getLeadingZeroes() );
    CHECK_EQ( 2, copy.getLeadingZeroes() );
    FileNameContent assigned("x");
    assigned = c;
    CHECK_EQ( 2, assigned.getLeadingZeroes() );
    CHECK_EQ( std::string("/d/img_0042.exr"), assigned.absoluteFileName() );

    SequenceFromFiles seq(false);
    insertAll( seq, numbered("/d/img_", 1, 3, 4, ".exr") );
    SequenceFromFiles seqCopy(seq);
    CHECK_EQ( std::string("/d/img_####.exr"), seqCopy.generateValidSequencePattern() );
    CHECK_EQ( 3, seqCopy.count() );
    CHECK( seqCopy.tryInsertFile( FileNameContent("/d/img_0004.exr") ) );
}

static void
testFilePatternNumHashes()
{
    // getFilePattern() must not return a pattern computed for another number of hashes
    FileNameContent c("/d/a1_b001.exr");
    CHECK_EQ( std::string("a#0_b#1.exr"), c.getFilePattern(1) );
    CHECK_EQ( std::string("a###0_b###1.exr"), c.getFilePattern(3) );
    std::string pattern;
    c.generatePatternWithFrameNumberAtIndex(1, 3, &pattern);
    CHECK_EQ( std::string("/d/a1_b###.exr"), pattern );
    c.generatePatternWithFrameNumberAtIndex(1, 4, &pattern);
    CHECK_EQ( std::string("/d/a1_b####.exr"), pattern );
}

static void
testPatternAtIndex()
{
    // the frame number is not always the last number
    FileNameContent c("/d/shot1_v22_0003.exr");
    std::string pattern;
    c.generatePatternWithFrameNumberAtIndex(0, 2, &pattern);
    CHECK_EQ( std::string("/d/shot##_v22_0003.exr"), pattern );
    c.generatePatternWithFrameNumberAtIndex(1, 2, &pattern);
    CHECK_EQ( std::string("/d/shot1_v##_0003.exr"), pattern );
    c.generatePatternWithFrameNumberAtIndex(2, 4, &pattern);
    CHECK_EQ( std::string("/d/shot1_v22_####.exr"), pattern );
    // more than 10 numbers: the pattern index has two digits
    FileNameContent many("1_2_3_4_5_6_7_8_9_10_11_12.exr");
    CHECK_EQ( 12, many.getPotentialFrameNumbersCount() );
    many.generatePatternWithFrameNumberAtIndex(11, 2, &pattern);
    CHECK_EQ( std::string("1_2_3_4_5_6_7_8_9_10_11_##.exr"), pattern );
    many.generatePatternWithFrameNumberAtIndex(10, 2, &pattern);
    CHECK_EQ( std::string("1_2_3_4_5_6_7_8_9_10_##_12.exr"), pattern );
}

static void
testSizeEstimation()
{
    // size estimation of files that can't be read
    SequenceFromFiles seq( FileNameContent("/nonexistent-dir/img_0001.exr"), true );
    seq.tryInsertFile( FileNameContent("/nonexistent-dir/img_0002.exr") );
    CHECK_EQ( 2, seq.count() );
    CHECK_EQ( 0ULL, seq.getEstimatedTotalSize() );
#if !defined(_WIN32)
    char tmpl[] = "/tmp/SequenceParsingSize.XXXXXX";
    char* dir = mkdtemp(tmpl);
    CHECK(dir != NULL);
    if (dir) {
        std::string f = std::string(dir) + "/img_0001.exr";
        {
            std::ofstream out( f.c_str() );
            out << "12345";
        }
        SequenceFromFiles s2( FileNameContent(f), true );
        CHECK_EQ( 5ULL, s2.getEstimatedTotalSize() );
        std::remove( f.c_str() );
        rmdir(dir);
    }
#endif
}

static void
testIncompatibleOtherNumber()
{
    // a number that differs but can't share the padding of the other one means another sequence:
    // a8_002.exr is not frame 2 of a08_###.exr
    int idx = -1;
    CHECK( !FileNameContent("a8_002.exr").matchesPattern(FileNameContent("a08_001.exr"), &idx) );
    CHECK( !FileNameContent("a08_001.exr").matchesPattern(FileNameContent("a8_002.exr"), &idx) );
    SequenceFromFiles seq( FileNameContent("/d/a08_001.exr"), false );
    CHECK( !seq.tryInsertFile( FileNameContent("/d/a8_002.exr") ) );
    CHECK( seq.tryInsertFile( FileNameContent("/d/a08_002.exr") ) );
    CHECK( seq.tryInsertFile( FileNameContent("/d/a08_010.exr") ) );
    CHECK_EQ( std::string("/d/a08_###.exr"), seq.generateValidSequencePattern() );
}

static void
testNumberInExtension()
{
    // numbers in the extension are not frame numbers
    const char* exts[] = { ".CR2", ".jp2", ".cr3", ".j2k", ".3fr", ".x3f", ".mp4" };
    for (int e = 0; e < 7; ++e) {
        SequenceFromFiles seq(false);
        CHECK_EQ( 3, insertAll( seq, numbered("/d/IMG_", 1, 3, 4, exts[e]) ) );
        CHECK_EQ( std::string("/d/IMG_####") + exts[e], seq.generateValidSequencePattern() );
        CHECK_EQ( 3, (int)parse( "IMG_####" + std::string(exts[e]), numbered("IMG_", 1, 3, 4, exts[e]) ).size() );
    }
    FileNameContent c("/d/clip.mp4");
    CHECK_EQ( 0, c.getPotentialFrameNumbersCount() );
    CHECK_EQ( std::string("mp4"), c.getExtension() );
    CHECK_EQ( std::string("clip.mp4"), c.getFilePattern(3) );
    SequenceFromFiles single( FileNameContent("/d/a.mp3"), false );
    CHECK( !single.tryInsertFile( FileNameContent("/d/a.mp4") ) );
    FileNameContent noext("/d/img0001");
    CHECK_EQ( 1, noext.getPotentialFrameNumbersCount() );
}

static void
testOverflowFirstFile()
{
    // a first file whose number doesn't fit in an int has no frame number: nothing joins it
    SequenceFromFiles seq( FileNameContent("/d/t_4294967297.exr"), false );
    CHECK( !seq.tryInsertFile( FileNameContent("/d/t_1.exr") ) );
    CHECK( !seq.tryInsertFile( FileNameContent("/d/t_4294967298.exr") ) );
    CHECK( seq.isSingleFile() );
    CHECK_EQ( std::string("/d/t_4294967297.exr"), seq.generateValidSequencePattern() );
}

static void
testMaxPadding()
{
    // paddings wider than 64 make the pattern invalid (a %0999999999d name would be 1 GB)
    std::vector<std::string> views;
    CHECK_EQ( std::string(63, '0') + "7.exr", generateFileNameFromPattern("%064d.exr", views, 7, 0) );
    const char* invalid[] = { "%065d.exr", "%0999999999d.exr", "%0-5d.exr" };
    for (int i = 0; i < 3; ++i) {
        bool thrown = false;
        try {
            generateFileNameFromPattern(invalid[i], views, 7, 0);
        } catch (const std::invalid_argument&) {
            thrown = true;
        }
        CHECK(thrown);
    }
    StringList files;
    files.push_back(std::string(64, '1') + ".exr");
    files.push_back("1.exr");
    CHECK_EQ( 0, (int)parse("%065d.exr", files).size() );
    CHECK_EQ( 0, (int)parse("%0999999999d.exr", files).size() );
}

static void
testIntLimitsRoundTrip()
{
    // INT_MIN and INT_MAX are read back
    std::vector<std::string> views;
    StringList files;
    files.push_back( generateFileNameFromPattern("img.####.exr", views, INT_MIN, 0) );
    files.push_back( generateFileNameFromPattern("img.####.exr", views, INT_MAX, 0) );
    files.push_back("img.-2147483649.exr");
    SequenceFromPattern seq = parse("img.####.exr", files);
    CHECK_EQ( 2, (int)seq.size() );
    CHECK( seq.find(INT_MIN) != seq.end() );
    CHECK( seq.find(INT_MAX) != seq.end() );
}

static void
testAllDigitExtension()
{
    // an extension made only of digits is the frame number (img.0001, img.0002...)
    SequenceFromFiles seq(false);
    CHECK_EQ( 3, insertAll( seq, numbered("/d/img.", 1, 3, 4, "") ) );
    CHECK_EQ( std::string("/d/img.####"), seq.generateValidSequencePattern() );
    FileNameContent c("/d/img.0010");
    CHECK_EQ( 1, c.getPotentialFrameNumbersCount() );
    CHECK_EQ( 2, c.getLeadingZeroes() );
    std::string pattern;
    // as in Natron's conversion of old file keyframes (KnobSerialization)
    c.generatePatternWithFrameNumberAtIndex(c.getPotentialFrameNumbersCount() - 1, c.getLeadingZeroes() + 1, &pattern);
    CHECK_EQ( std::string("/d/img.###"), pattern ); // same as before 7ca1e46
    FileNameContent c2("/d/shot_v2.0010");
    c2.generatePatternWithFrameNumberAtIndex(c2.getPotentialFrameNumbersCount() - 1, 4, &pattern);
    CHECK_EQ( std::string("/d/shot_v2.####"), pattern );
    // and the generated names are as before
    std::vector<std::string> views;
    CHECK_EQ( std::string("/d/img.0010"), generateFileNameFromPattern("/d/img.####", views, 10, 0) );
}

#if !defined(_WIN32)
static void
testDirectoryListing()
{
    char tmpl[] = "/tmp/SequenceParsingTest.XXXXXX";
    char* dir = mkdtemp(tmpl);

    CHECK(dir != NULL);
    if (!dir) {
        return;
    }
    std::string d(dir);
    StringList files = numbered("", 1, 3, 4, ".exr");
    for (size_t i = 0; i < files.size(); ++i) {
        std::ofstream( (d + "/" + files[i]).c_str() );
    }
    mkdir( (d + "/0004.exr").c_str(), 0700 ); // directories are ignored
    SequenceFromPattern seq;
    CHECK( filesListFromPattern_slow(d + "/####.exr", &seq) );
    CHECK_EQ( 3, (int)seq.size() );
    for (size_t i = 0; i < files.size(); ++i) {
        std::remove( (d + "/" + files[i]).c_str() );
    }
    rmdir( (d + "/0004.exr").c_str() );
    rmdir( d.c_str() );
    seq.clear();
    CHECK( !filesListFromPattern_slow(d + "/####.exr", &seq) );
}
#endif

int
main()
{
    testPatterns();
    testViews();
    testGenerate();
    testFileNameContent();
    testSequenceFromFiles();
    testUtf8();
    testIntOverflow();
    testPrintfNoPadding();
    testViewsRoundTrip();
    testNegativeFrames();
    testGlobalLocale();
    testUnpaddedWithZero();
    testSequencePadding();
    testCopies();
    testFilePatternNumHashes();
    testPatternAtIndex();
    testSizeEstimation();
    testIncompatibleOtherNumber();
    testNumberInExtension();
    testOverflowFirstFile();
    testMaxPadding();
    testIntLimitsRoundTrip();
    testAllDigitExtension();
#if !defined(_WIN32)
    testDirectoryListing();
#endif
    std::cout << g_checks << " checks, " << g_failures << " failures" << std::endl;

    return g_failures ? 1 : 0;
}
