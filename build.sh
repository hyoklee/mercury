export CMAKE_PREFIX_PATH=$PREFIX
export CMAKE_INCLUDE_PATH=$PREFIX/include
# export PKG_CONFIG_PATH=$CONDA_PREFIX/lib/pkgconfig:$PKG_CONFIG_PATH
mkdir build
cd build
cmake \
    -DMERCURY_USE_BOOST_PP=ON \
    -DNA_USE_OFI=OFF \
    -DCMAKE_INSTALL_PREFIX:PATH=$PREFIX ..
cmake --build . --config Release 
cmake --install . --config Release 
