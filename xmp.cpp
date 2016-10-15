/*-
 * Copyright (c) 2015 Chris Spiegel
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <memory>
#include <sstream>

#include <libaudcore/audstrings.h>
#include <libaudcore/i18n.h>
#include <libaudcore/plugin.h>
#include <libaudcore/preferences.h>
#include <libaudcore/runtime.h>

#include "xmpwrap.h"

static Index<ComboItem> items;

static bool force_apply = false;

static const char *SETTING_STEREO_SEPARATION = "stereo_separation";
static const char *SETTING_PANNING_AMPLITUDE = "panning_amplitude";
static const char *SETTING_INTERPOLATOR      = "interpolator";

class XMPPlugin : public InputPlugin
{
  public:
    static const char about[];
    static const char *const exts[];
    static const PreferencesWidget widgets[];
    static const PluginPreferences prefs;

    static constexpr PluginInfo info =
    {
      N_("XMP (Module Player)"),
      PACKAGE,
      about,
      &prefs,
    };

    static constexpr auto iinfo = InputInfo(0).with_exts(exts);

    constexpr XMPPlugin() : InputPlugin(info, iinfo) { }

    bool init()
    {
      std::ostringstream default_stereo_separation;
      std::ostringstream default_panning_amplitude;
      std::ostringstream default_interpolator;

      default_stereo_separation << XMPWrap::default_stereo_separation();
      default_panning_amplitude << XMPWrap::default_panning_amplitude();
      default_interpolator << XMPWrap::default_interpolator();

      const char *const defaults[] =
      {
        SETTING_STEREO_SEPARATION, default_stereo_separation.str().c_str(),
        SETTING_PANNING_AMPLITUDE, default_panning_amplitude.str().c_str(),
        SETTING_INTERPOLATOR, default_interpolator.str().c_str(),
        nullptr
      };

      aud_config_set_defaults(PACKAGE, defaults);

      return true;
    }

    bool is_our_file(const char *filename, VFSFile &file)
    {
      auto xmp = open_file(filename, file);

      if(!xmp) return false;

      return true;
    }

    bool read_tag(const char *filename, VFSFile &file, Tuple &tuple, Index<char> *)
    {
      auto xmp = open_file(filename, file);

      if(xmp)
      {
        tuple.set_filename(filename);
        tuple.set_format(xmp->format().c_str(), xmp->channels(), xmp->rate(), 0);

        tuple.set_int(Tuple::Length, xmp->duration());

        if(!xmp->title().empty()) tuple.set_str(Tuple::Title, xmp->title().c_str());

        return true;
      }
      else
      {
        return false;
      }
    }

    bool play(const char *filename, VFSFile &file)
    {
      auto xmp = open_file(filename, file, aud_get_int(PACKAGE, SETTING_PANNING_AMPLITUDE));

      if(!xmp) return false;

      force_apply = true;

      open_audio(FMT_S16_NE, 44100, 2);

      while(!check_stop())
      {
        int seek_value = check_seek();

        if(seek_value >= 0) xmp->seek(seek_value);

        if(force_apply)
        {
          xmp->set_interpolator(aud_get_int(PACKAGE, SETTING_INTERPOLATOR));
          xmp->set_stereo_separation(aud_get_int(PACKAGE, SETTING_STEREO_SEPARATION));
          force_apply = false;
        }

        XMPWrap::Frame frame = xmp->play_frame();

        if(frame.n == 0) break;

        write_audio(frame.buf, frame.n);
      }

      return true;
    }

  private:
    /* libxmp doesn't have a stream interface to support VFSFile, so the
     * only way to handle a VFSFile is by reading all data from it.
     * This requires either a cap on the size (which could prevent some
     * files from loading) or allowing memory to blow up in size.
     * Therefore, if the file is local, try opening it directly, first.
     * Only if that fails (or it is not local) is the VFSFile read from.
     * The read_all() method is used, which, as of Audacious 3.6.2, has
     * a 16MB limit.
     */
    std::unique_ptr<XMPWrap> open_file(const char *uri, VFSFile &file, int panning_amplitude = -1)
    {
      XMPWrap *xmp = nullptr;
      StringBuf filename(uri_to_filename(uri, false));

      if(filename.len() > 0)
      {
        try
        {
          xmp = new XMPWrap(std::string((char *)filename), panning_amplitude);
        }
        catch(XMPWrap::InvalidFile)
        {
        }
      }

      if(xmp == nullptr)
      {
        Index<char> buf = file.read_all();
        try
        {
          xmp = new XMPWrap(buf.begin(), buf.len(), panning_amplitude);
        }
        catch(XMPWrap::InvalidFile)
        {
        }
      }

      return std::unique_ptr<XMPWrap>(xmp);
    }
};

const char XMPPlugin::about[] = N_("Module player based on libxmp\n\nWritten by: Chris Spiegel <cspiegel@gmail.com>");

const char *const XMPPlugin::exts[] =
{
  "669", "amf", "dbm", "digi", "emod", "far", "fnk", "gdm", "gmc", "imf",
  "ims", "it", "j2b", "liq", "mdl", "med", "mgt", "mod", "mtm", "ntp", "oct",
  "okta", "psm", "ptm", "rad", "rtm", "s3m", "stm", "ult", "umx", "xm",
  nullptr
};

static ArrayRef<ComboItem> interpolator_fill()
{
  items.clear();

  for(const XMPWrap::Interpolator &interpolator : XMPWrap::get_interpolators())
  {
    items.append(interpolator.name, interpolator.value);
  }

  return { items.begin(), items.len() };
}

static void values_changed()
{
  force_apply = true;
}

const PreferencesWidget XMPPlugin::widgets[] =
{
  WidgetSpin(
    N_("Stereo separation:"),
    WidgetInt(PACKAGE, SETTING_STEREO_SEPARATION, values_changed),
    { 0.0, 100.0, 1.0, N_("%") }
  ),

  WidgetSpin(
    N_("Panning amplitude:"),
    WidgetInt(PACKAGE, SETTING_PANNING_AMPLITUDE),
    { 0.0, 100.0, 1.0, N_("%") }
  ),

  WidgetCombo(
    N_("Interpolation:"),
    WidgetInt(PACKAGE, SETTING_INTERPOLATOR, values_changed),
    { nullptr, interpolator_fill }
  )
};

const PluginPreferences XMPPlugin::prefs = {{ widgets }};

XMPPlugin aud_plugin_instance;
