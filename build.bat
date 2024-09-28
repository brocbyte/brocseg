conan install . --output-folder=build --build="missing" --settings=build_type=Debug
pushd build
cmake .. -G "Visual Studio 17 2022" -DCMAKE_TOOLCHAIN_FILE="conan_toolchain.cmake" -DCMAKE_POLICY_DEFAULT_CMP0091=NEW
cmake --build . --config Debug
popd
