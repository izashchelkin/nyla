pushd build

glslc ../shader.vert -o vert.spv
glslc ../shader.frag -o frag.spv

glslc ../grid.vert -o grid_vert.spv
glslc ../grid.frag -o grid_frag.spv

popd