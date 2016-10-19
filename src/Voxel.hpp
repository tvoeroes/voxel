#pragma once

#include "Window.hpp"
#include "World.hpp"
#include "Shader.hpp"
#include "Camera.hpp"
#include "Player.hpp"
#include "TextureArray.hpp"

//==============================================================================
class Voxel
{
public:
    Voxel(const char * location);

    void run();

private:
    Window m_window;
    World m_world;
    Shader m_block_shader;
    Camera<float> m_camera;
    GLint m_block_VP_matrix_location;
    Player m_player;
    TextureArray m_block_textures;
    GLint block_texture_array_location;

};
