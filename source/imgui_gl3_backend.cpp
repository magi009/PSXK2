// Compila el backend OpenGL3 de Dear ImGui usando glad como cargador de funciones
// (el cargador interno de ImGui usa dlopen, que no existe en Switch).
#include <glad/glad.h>
#include "imgui_impl_opengl3.cpp"
