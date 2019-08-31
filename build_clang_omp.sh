clang -fopenmp=libomp -L../llvm-project/build/lib/ -Iinclude -std=c++17 -O2 sample.cpp lib/source.cpp -lstdc++ -lpthread -lm -o sample
