# SequenceParsing

## Definition:

Pattern: A file name, absolute or not, which is composed of symbols (variable) which
can take several values.

### Supported variables:

- %d : indicates that any number should be present instead of the variable. This option
works like printf like arguments, e.g:  %04d, which indicates you want the variable
to match only numbers written with 4 digits, padded with leading zeroes (numbers with
more digits are matched if they have no leading zero). %d matches numbers without padding.

- \#\#\# : Same as %0Nd, where N is the number of "#" characters, e.g. #### is %04d.

Negative frame numbers are written and matched as printf does: frame -5 is "-005" with %04d or ####
(older versions wrote "00-5"). A '-' right before a frame number variable is read as its sign:
img###.exr matches img-01.exr as frame -1. Paddings wider than 64 (%065d) and negative paddings (%0-5d)
make the pattern invalid: generateFileNameFromPattern() throws std::invalid_argument.

- %v: indicates that a short view name should be present instead of the variable.
This means either "l" for the left view and "r" for the right view, or "view<N>" for view number N.
When matching file names, the case is ignored ("L" and "R" are what %v writes).

- %V: Same as %v but for long view names, i.e: "right" and "left" (or "Right", "Left"...)


This repository aims to help solving 3 problems:

1) Given a file name, search within the same directory for all other files that matches
the same "pattern".

2) Given a "pattern" generate file names for a given frame number and view.

3) Given a files list, tries to group files under similar patterns.
The frame number is the last number of the file name that varies, not counting the extension
(IMG_0001.CR2 is frame 1 of IMG_####.CR2).



## Tests

The tests have no dependency besides the C++ standard library:

    cmake -S tests -B build-tests && cmake --build build-tests && ctest --test-dir build-tests --output-on-failure
