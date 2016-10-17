/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2010-2014 - Hans-Kristian Arntzen
 *  Copyright (C) 2011-2016 - Daniel De Matteis
 *
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with RetroArch.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#include "font_driver.h"
#include "video_thread_wrapper.h"

#include "../configuration.h"
#include "../verbosity.h"

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#include <rhash.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <retro_assert.h>

struct font_t
{
   const font_renderer_driver_t *backend;
   void *backend_data;

   const font_renderer_t *renderer;
   void *renderer_data;

   unsigned path_hash;
   char *path;
   float size;

   font_t *next;
   unsigned refcount;
};

static const font_renderer_driver_t *font_backends[] = {
#ifdef HAVE_FREETYPE
   &freetype_font_renderer,
#endif
#if defined(__APPLE__) && defined(HAVE_CORETEXT)
   &coretext_font_renderer,
#endif
#ifdef HAVE_STB_FONT
   &stb_font_renderer,
#endif
   &bitmap_font_renderer,
   NULL
};

#if 0
static const struct font_renderer *font_osd_driver;
static void *font_osd_data;
static font_t *g_last_font          = NULL;
#endif

static font_t *g_fonts              = NULL;
static const font_t *g_default_font = NULL;
static enum font_driver_render_api g_renderer_api = FONT_DRIVER_RENDER_DONT_CARE;


int font_renderer_create_default(const void **data, void **handle,
      const char *font_path, unsigned font_size)
{

   unsigned i;
   const font_renderer_driver_t **drv =
      (const font_renderer_driver_t**)data;

   for (i = 0; font_backends[i]; i++)
   {
      const char *path = font_path;

      if (!path)
         path = font_backends[i]->get_default_font();
      if (!path)
         continue;

      *handle = font_backends[i]->init(path, font_size);
      if (*handle)
      {
         RARCH_LOG("Using font rendering backend: %s.\n",
               font_backends[i]->ident);
         *drv = font_backends[i];
         return 1;
      }
      else
         RARCH_ERR("Failed to create rendering backend: %s.\n",
               font_backends[i]->ident);
   }

   *drv = NULL;
   *handle = NULL;

   return 0;
}

#ifdef HAVE_D3D
static const font_renderer_t *d3d_font_backends[] = {
#if defined(_XBOX1)
   &d3d_xdk1_font,
#elif defined(_XBOX360)
   &d3d_xbox360_font,
#elif defined(_WIN32)
   &d3d_win32_font,
#endif
};

static bool d3d_font_init_first(
      const font_renderer_t **font_driver, void **font_handle,
      void *video_data, const font_t *font_data)
{
   unsigned i;

   for (i = 0; i < ARRAY_SIZE(d3d_font_backends); i++)
   {
      void *data = d3d_font_backends[i]->init(
            video_data, font_path, font_size);

      if (!data)
         continue;

      *font_driver = d3d_font_backends[i];
      *font_handle = data;

      return true;
   }

   return false;
}
#endif

#ifdef HAVE_OPENGL
static const font_renderer_t *gl_font_backends[] = {
   &gl_raster_font,
#if defined(HAVE_LIBDBGFONT)
   &libdbg_font,
#endif
   NULL,
};

static bool gl_font_init_first(
      const font_renderer_t **font_driver, void **font_handle,
      void *video_data, const font_t *font_data)
{
   unsigned i;

   for (i = 0; gl_font_backends[i]; i++)
   {
      void *data = gl_font_backends[i]->init(video_data, font_data);

      if (!data)
         continue;

      *font_driver = gl_font_backends[i];
      *font_handle = data;
      return true;
   }

   return false;
}
#endif

#ifdef HAVE_VULKAN
static const font_renderer_t *vulkan_font_backends[] = {
   &vulkan_raster_font,
   NULL,
};

static bool vulkan_font_init_first(
      const font_renderer_t **font_driver, void **font_handle,
      void *video_data, const font_t *font_data)
{
   unsigned i;

   for (i = 0; vulkan_font_backends[i]; i++)
   {
      void *data = vulkan_font_backends[i]->init(video_data, font_data);

      if (!data)
         continue;

      *font_driver = vulkan_font_backends[i];
      *font_handle = data;
      return true;
   }

   return false;
}
#endif

#ifdef HAVE_VITA2D
static const font_renderer_t *vita2d_font_backends[] = {
   &vita2d_vita_font
};

static bool vita2d_font_init_first(
      const font_renderer_t **font_driver, void **font_handle,
      void *video_data, const font_t *font_data)
{
   unsigned i;

   for (i = 0; vita2d_font_backends[i]; i++)
   {
      void *data = vita2d_font_backends[i]->init(video_data, font_data);

      if (!data)
         continue;

      *font_driver = vita2d_font_backends[i];
      *font_handle = data;
      return true;
   }

   return false;
}
#endif

#ifdef _3DS
static const font_renderer_t *ctr_font_backends[] = {
   &ctr_font
};

static bool ctr_font_init_first(
      const font_renderer_t **font_driver, void **font_handle,
      void *video_data, const font_t *font_data)
{
   unsigned i;

   for (i = 0; ctr_font_backends[i]; i++)
   {
      void *data = ctr_font_backends[i]->init(video_data, font_data);

      if (!data)
         continue;

      *font_driver = ctr_font_backends[i];
      *font_handle = data;
      return true;
   }

   return false;
}
#endif

static bool font_init_first(
      const font_renderer_t **font_driver, void **font_handle,
      void *video_data, const font_t *font_data)
{
   switch (g_renderer_api)
   {
#ifdef HAVE_D3D
      case FONT_DRIVER_RENDER_DIRECT3D_API:
         return d3d_font_init_first(font_driver, font_handle,
               video_data, font);
#endif
#ifdef HAVE_OPENGL
      case FONT_DRIVER_RENDER_OPENGL_API:
         return gl_font_init_first(font_driver, font_handle,
               video_data, font_data);
#endif
#ifdef HAVE_VULKAN
      case FONT_DRIVER_RENDER_VULKAN_API:
         return vulkan_font_init_first(font_driver, font_handle,
               video_data, font_data);
#endif
#ifdef HAVE_VITA2D
      case FONT_DRIVER_RENDER_VITA2D:
         return vita2d_font_init_first(font_driver, font_handle,
               video_data, font_data);
#endif
#ifdef _3DS
      case FONT_DRIVER_RENDER_CTR:
         return ctr_font_init_first(font_driver, font_handle,
               video_data, font_data);
#endif
      case FONT_DRIVER_RENDER_DONT_CARE:
         /* TODO/FIXME - lookup graphics driver's 'API' */
         break;
      default:
         break;
   }

   return false;
}

bool font_driver_has_render_msg(void)
{
   return true;
}

static INLINE font_t *font_check(const font_t *font, bool need_renderer)
{
   font_t *f = (font_t*)font;

   if (f == NULL)
      f = (font_t*)g_default_font;

   if (need_renderer && f)
   {
      if (!f->renderer)
      {
         const settings_t *settings = config_get_ptr();
         bool is_video_thread = video_thread_is_video_thread();

         if (!is_video_thread && settings->video.threaded && !video_driver_is_hw_context())
            video_thread_font_init(&f->renderer, &f->renderer_data, video_driver_get_ptr(false), f, font_init_first);
         else
            font_init_first(&f->renderer, &f->renderer_data, video_driver_get_ptr(false), f);
      }

      return f->renderer ? f : NULL;
   }

   return f;
}

bool font_driver_render_msg(const font_t *font,
      const char *msg, const struct font_params *params)
{
   font_t *f;

   retro_assert(font || g_default_font);

   f = font_check(font, true);

   if (!f)
      return false;

   f->renderer->render_msg(f->renderer_data, msg, params);

   return true;
}


void font_driver_bind_block(const font_t *font, void *block)
{
   font = font_check(font, true);
   if (font && font->renderer->bind_block)
      font->renderer->bind_block(font->renderer_data, block);
}

void font_driver_flush(const font_t *font)
{
   font = font_check(font, true);
   if (font && font->renderer->flush)
      font->renderer->flush(font->renderer_data);
}

int font_driver_get_message_width(const font_t *font,
      const char *msg, unsigned len, float scale)
{
   font = font_check(font, true);
   if (font && font->renderer->get_message_width)
      return font->renderer->get_message_width(font->renderer_data, msg, len, scale);

   return -1;
}

const char *font_driver_get_path(const font_t *font)
{
   font = font_check(font, false);
   return font->path;
}

float font_driver_get_size(const font_t *font)
{
   font = font_check(font, false);
   return font->size;
}


const struct font_atlas *font_driver_get_atlas(const font_t *font)
{
   font = font_check(font, false);
   return font->backend->get_atlas(font->backend_data);
}

const struct font_glyph *font_driver_get_glyph(const font_t *font, unsigned codepoint)
{
   font = font_check(font, false);
   return font->backend->get_glyph(font->backend_data, codepoint);
}

int font_driver_get_line_height(const font_t *font)
{
   font = font_check(font, false);
   if (font->backend->get_line_height)
      return font->backend->get_line_height(font->backend_data);

   return -1;
}

void font_driver_free(void *data)
{
#if 0
   if (font_osd_driver && font_osd_driver->free)
      font_osd_driver->free(data ? data : font_osd_data);

   if (data)
      return;

   font_osd_data   = NULL;
   font_osd_driver = NULL;
#endif
}

void font_driver_set_api(enum font_driver_render_api api)
{
   if (g_renderer_api == FONT_DRIVER_RENDER_DONT_CARE)
   {
      g_renderer_api = api;
      RARCH_LOG("[font] Using font api %i\n", api);
   }
}

void font_driver_init_default(void)
{
   const settings_t *settings = config_get_ptr();
   const char *filename = *settings->path.font ? settings->path.font : NULL;

   g_default_font = font_driver_load(filename, settings->video.font_size);
}

void font_driver_deinit_default(void)
{
   font_driver_unload(g_default_font);
   g_default_font = NULL;
}

const font_t *font_driver_load(const char *filename, float size)
{
   const unsigned path_hash = djb2_calculate(filename ? filename : "");
   font_t *font             = NULL;
   int i;

   if (fabs(size) < 1.f)
   {
      const float FALLBACK = 10.0f;

      RARCH_WARN("[font] Requested font size for %s is too small: %.2f. "
             "Using %.2f.\n", filename ? filename : "", size, FALLBACK);

      size = FALLBACK;
   }

   if (g_fonts) /* search existing instance */
   {
      for (font = g_fonts; font; font = font->next)
      {
         if (font->path_hash == path_hash && fabs(font->size - size) <= 0.001f)
         {
            font->refcount += 1;
            return font;
         }
      }

      font = NULL;
   }

   /* create new instance */
   for (i = 0; font_backends[i]; ++i)
   {
      const font_renderer_driver_t *backend = font_backends[i];
      const char *path = filename;
      void *data;

      if (!path || !*path)
         path = backend->get_default_font();

      if (!path)
         continue;

      data = backend->init(path, size);

      if (!data)
         continue;

      font = (font_t*)calloc(1, sizeof(*font));

      if (!font)
      {
         RARCH_ERR("[font] Allocation failed "
                   "(path=%s size=%.2f backend=%s): %s\n",
                   path, backend->ident, size, strerror(errno));

         return NULL;
      }

      font->path         = strdup(path);
      font->path_hash    = path_hash;
      font->size         = size;
      font->backend      = backend;
      font->backend_data = data;

      font->refcount = 1;

      font->next = g_fonts;
      g_fonts    = font->next;

      RARCH_LOG("[font] Loaded %s (size=%.2f backend=%s)\n",
                font->path, font->size, font->backend->ident);

      return font;
   }

   RARCH_WARN("[font] Failed to load %s (size=%.2f): no working backend.\n",
       filename, size);

   return NULL;
}

void font_driver_unload(const font_t *font)
{
   font_t *f = (font_t *)font; /* yes */

   retro_assert(font != NULL);
   retro_assert(font->refcount > 0);

   if (--f->refcount)
      return;

   if (g_fonts)
   {
      font_t *entry = g_fonts;
      font_t *prev  = NULL;

      while (entry)
      {
         if (entry == f)
         {
            if (prev)
               prev->next = entry->next;
            break;
         }

         prev  = entry;
         entry = entry->next;
      }
   }

   if (f->renderer_data)
      f->renderer->free(f->renderer_data);

   f->backend->free(f->backend_data);

   if (f->path)
      free(f->path);

   f->renderer      = NULL;
   f->renderer_data = NULL;

   f->backend      = NULL;
   f->backend_data = NULL;

   f->path = NULL;

   if (f == g_default_font)
      g_default_font = NULL;

   if (f == g_fonts)
       g_fonts = NULL;

   free(f);
}

void font_driver_context_reseted(void)
{
   font_t *font;
   for (font = g_fonts; font; font = font->next)
   {
      if (font->renderer)
      {
         font->renderer->free(font->renderer_data);

         font->renderer      = NULL;
         font->renderer_data = NULL;
      }
   }
}
