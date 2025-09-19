find . -name "*.[chsS]" -exec sed 's/\s\+$//g' -i {} \;
find . -name "*.[chsS]" -exec sed 's/\t/    /g' -i {} \;
find . -name "*.[ch]" -exec clang-format -i {} \;
