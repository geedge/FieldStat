scriptfolder=`pwd`
cd build
rm -rf *
cmake -DCMAKE_BUILD_TYPE=Release .. 
# cmake .. -DCOVERAGE=True
make
cd test
for f in *test*; do
    echo "Running $f"
    ./$f
    # valgrind --tool=memcheck --leak-check=full ./$f
    wait
done
# lcov --capture --directory . --output-file AnyName.info
# genhtml AnyName.info --output-directory CODE_COVERAGE

cd $scriptfolder
