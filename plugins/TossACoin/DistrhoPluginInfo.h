/*
 * TossACoin — DPF plugin info header.
 *
 * Single source of truth for the plugin's LV2 identity. Change every
 * `tossacoin` string here AND the matching values in the top-level
 * Makefile (PLUGIN, BRAND, LABEL, PLUGIN_URI_BASE) when forking.
 *
 * BETA build: pass -DTOSSACOIN_BETA on the command line (top-level
 * Makefile does this when BETA=1) to get a side-by-side build with a
 * distinct LV2 URI, bundle name, label, and DPF unique-id — installs
 * alongside the stable plugin for A/B testing.
 */

#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

#ifdef TOSSACOIN_BETA
#define DISTRHO_PLUGIN_BRAND   "Stefan"
#define DISTRHO_PLUGIN_NAME    "Toss a Coin (Beta)"
#define DISTRHO_PLUGIN_URI     "http://tossacoin.local/plugins/tossacoin-beta"
#define DISTRHO_PLUGIN_CLAP_ID "local.tossacoin.tossacoin-beta"
// 4-char identifiers — must be unique within your set of plugins. DPF
// packs them into 32-bit ints (d_cconst). The beta gets distinct codes
// so DAWs / hosts don't confuse it with the stable plugin's state.
#define DISTRHO_PLUGIN_BRAND_ID  TosB
#define DISTRHO_PLUGIN_UNIQUE_ID dToB
#else
#define DISTRHO_PLUGIN_BRAND   "Stefan"
#define DISTRHO_PLUGIN_NAME    "Toss a Coin"
#define DISTRHO_PLUGIN_URI     "http://tossacoin.local/plugins/tossacoin"
#define DISTRHO_PLUGIN_CLAP_ID "local.tossacoin.tossacoin"
#define DISTRHO_PLUGIN_BRAND_ID  Toss
#define DISTRHO_PLUGIN_UNIQUE_ID dToC
#endif

// Real project homepage — hosts surface it (MOD's info dialog links its
// "See online" button here). The LV2 URI above is just an identifier, not
// a web page; returning it gives users a dead link. Change when forking.
#define PLUGIN_HOMEPAGE "https://github.com/stefdoerr/tossacoin"

// LV2 plugin class -> the "Category" shown in MOD's plugin info / store.
// Without it the plugin shows "Category: None". Pick the class matching
// your effect: lv2:DelayPlugin, lv2:DistortionPlugin, lv2:DynamicsPlugin,
// lv2:FilterPlugin, lv2:ModulatorPlugin, lv2:ReverbPlugin,
// lv2:SimulatorPlugin, lv2:SpatialPlugin, lv2:SpectralPlugin,
// lv2:GeneratorPlugin, ... (mod-ui maps these to its category names).
#define DISTRHO_PLUGIN_LV2_CATEGORY   "lv2:PitchPlugin"

// Feature flags. Adjust as your plugin grows.
#define DISTRHO_PLUGIN_HAS_UI         0    // 0 = no native UI; modgui supplies the GUI on MOD
#define DISTRHO_PLUGIN_IS_RT_SAFE     1    // 1 = no allocs / locks / I/O in run()
#define DISTRHO_PLUGIN_NUM_INPUTS     1    // mono input bus
#define DISTRHO_PLUGIN_NUM_OUTPUTS    2    // stereo output bus

#define DISTRHO_PLUGIN_WANT_PROGRAMS                          0
#define DISTRHO_PLUGIN_WANT_STATE                             0
// Enables requestParameterValueChange() — used when the DSP needs to
// echo parameter changes back to the host (e.g. for derived ports
// recomputed from other ports).
#define DISTRHO_PLUGIN_WANT_PARAMETER_VALUE_CHANGE_REQUEST    1

#endif // DISTRHO_PLUGIN_INFO_H_INCLUDED
