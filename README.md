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

Negative frame numbers are written and matched as printf does: frame -5 is "-005" with %04d or ####.

- %v: indicates that a short view name should be present instead of the variable.
This means either "l" for the left view and "r" for the right view, or "view<N>" for view number N.
When matching file names, the case is ignored ("L" and "R" are what %v writes).

- %V: Same as %v but for long view names, i.e: "right" and "left" (or "Right", "Left"...)


This repository aims to help solving 3 problems:

1) Given a file name, search within the same directory for all other files that matches
the same "pattern".

2) Given a "pattern" generate file names for a given frame number and view.

3) Given a files list, tries to group files under similar patterns.



## Tests

The tests have no dependency besides the C++ standard library:

    cmake -S tests -B build-tests && cmake --build build-tests && ctest --test-dir build-tests --output-on-failure
