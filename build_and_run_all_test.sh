scriptfolder=`pwd`
cd build
cmake ..
make
cd test
for f in *test*; do
    echo "Running $f"
    ./$f
done

cd $scriptfolder
