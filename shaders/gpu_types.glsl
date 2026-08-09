#extension GL_EXT_buffer_reference : require

struct Vertex
{
    vec3 position;
    float uv_x;
    vec3 normal;
    float uv_y;
    vec4 color;
};

struct VkDrawIndexedIndirectCommand {
    uint index_count;
    uint instance_count;
    uint first_index;
    int vertex_offset;
    uint first_instance;
};

layout(buffer_reference, std430) readonly buffer VertexBuffer
{
    Vertex vertices[];
};

struct GPUObjectData
{
    mat4 world_matrix;
    /**
     * Bounding sphere.
     *     - `.xyz` - Local origin.
     *     - `.w`   - Radius of the sphere.
     */
    vec4 sphere_bounds;
    VertexBuffer vertex_buffer;
};