export CMAKE_PREFIX_PATH=$PREFIX
export CMAKE_INCLUDE_PATH=$PREFIX/include
mkdir build
cd build
cmake \
    -DBUILD_SHARED_LIBS=ON \
    -DMERCURY_USE_BOOST_PP=ON \
    -DNA_USE_OFI=OFF \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DCMAKE_INSTALL_PREFIX:PATH=$PREFIX ..
cmake --build . --config Release 
cmake --install . --config Release 
