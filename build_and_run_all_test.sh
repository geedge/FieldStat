scriptfolder=`pwd`
cd build
cmake -DCMAKE_BUILD_TYPE=Release .. 
make
cd test
for f in *test*; do
    echo "Running $f"
    ./$f
    # valgrind --tool=memcheck --leak-check=full ./$f
done

cd $scriptfolder
