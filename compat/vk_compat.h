/* Constantes eliminadas de las cabeceras Vulkan modernas (OunceVK, Vulkan 1.4)
 * que el renderer Vulkan de Beetle PSX todavía usa. Valores originales del SDK. */
#pragma once
#ifndef VK_IMAGE_TYPE_RANGE_SIZE
#define VK_IMAGE_TYPE_RANGE_SIZE ((VkImageType)3)            /* 1D..3D */
#endif
#ifndef VK_IMAGE_VIEW_TYPE_RANGE_SIZE
#define VK_IMAGE_VIEW_TYPE_RANGE_SIZE ((VkImageViewType)7)   /* 1D..CUBE_ARRAY */
#endif
