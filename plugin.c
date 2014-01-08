#include <geanyplugin.h>
#include <stdbool.h>
#include <string.h>

GeanyPlugin    *geany_plugin;
GeanyData      *geany_data;
GeanyFunctions *geany_functions;

PLUGIN_VERSION_CHECK(211)

PLUGIN_SET_INFO("C Header Resolver",
                "Attempts to resolve conflicts between C, C++ and Objective-C "
                "header files which Geany cannot resolve.",
                "0.1",
                "Matthew Brush <matt@geany.org>")

enum
{
	LANG_C      = 0x01,
	LANG_CXX    = 0x02,
	LANG_OBJC   = 0x04,
	LANG_OBJCXX = 0x08,
};

struct match
{
	int langs;
	double rank; // 0.0 - 1.0
	const char *pattern;
	GRegex *re;
};

// TODO: move this out to a config file
static struct match matchers[] = {
	{ LANG_C,                  1.0, "-\\*-\\s*c\\s*-\\*-" },
	{ LANG_C | LANG_OBJC,      0.8, "#\\s*ifdef\\s+__cplusplus" },
	{ LANG_CXX,                1.0, "-\\*-\\s*c\\+\\+\\s*-\\*-" },
	{ LANG_CXX | LANG_OBJCXX,  0.8, "template\\s*\\<.*?\\>" },
	{ LANG_CXX | LANG_OBJCXX,  0.8, "\\s+class\\s+[a-zA-Z0-9_:]+" },
	{ LANG_CXX | LANG_OBJCXX,  0.8, "#\\s*include\\s+\\<[^\\.]+\\>" },
	{ LANG_OBJC,               1.0, "-\\*-\\s*objc\\s*-\\*-" },
	{ LANG_OBJC | LANG_OBJCXX, 0.8, "@end|@implementation|@interface|@property|@synthesize" },
	{ LANG_OBJC | LANG_OBJCXX, 0.5, "#\\s*include|import\\s+[\\\"<](.+?/)*Cocoa.h[\\\">]" },
	{ LANG_OBJC | LANG_OBJCXX, 0.5, "#\\s*include|import\\s+[\\\"<](.+?/)*Foundation.h[\\\">]" },
	{ LANG_OBJCXX,             1.0, "-\\*-\\s*objc\\+\\+\\s*-\\*-" },
	// ...
};

static const size_t num_matchers = sizeof(matchers) / sizeof(matchers[0]);

static void deinit_regexp(void)
{
	size_t i;
	for (i = 0; i < num_matchers; i++)
	{
		if (matchers[i].re != NULL)
		{
			g_regex_unref(matchers[i].re);
			matchers[i].re = NULL;
		}
	}
}

static void init_regexp(void)
{
	size_t i;
	deinit_regexp();
	for (i = 0; i < num_matchers; i++)
	{
		GError *error = NULL;
		matchers[i].re = g_regex_new(matchers[i].pattern, 0, 0, &error);
		if (error != NULL)
		{
			g_warning("failed to compile regex: %s", error->message);
			g_error_free(error);
		}
	}
}

static GeanyFiletype *detect_filetype(GeanyDocument *doc)
{
	GeanyFiletype *ft = doc->file_type;
	ScintillaObject *sci = doc->editor->sci;
	size_t length = sci_get_length(sci);
	const char *text = (const char*) scintilla_send_message(sci, SCI_GETCHARACTERPOINTER, 0, 0);
	gdouble c_value = 0.0, c_total = 0.0;
	gdouble cxx_value = 0.0, cxx_total = 0.0;
	gdouble objc_value = 0.0, objc_total = 0.0;
	gdouble objcxx_value = 0.0, objcxx_total = 0.0;

	unsigned int i;
	for (i = 0; i < num_matchers; i++)
	{
		gboolean matched = g_regex_match_full(matchers[i].re, // GRegex
		                                      text,           // string to match
		                                      length,         // string length
		                                      0,              // start pos
		                                      0,              // match flags
		                                      NULL,           // match info
		                                      NULL);          // GError

		int m_langs = matchers[i].langs;

		if (matched)
			geany_debug("Match for Pattern: %s", matchers[i].pattern);
		else
			geany_debug("No match for pattern: %s", matchers[i].pattern);

		if (m_langs & LANG_C)
		{
			c_total += 1.0;
			c_value += matched ? matchers[i].rank : 0.0;
		}
		if (m_langs & LANG_CXX)
		{
			cxx_total += 1.0;
			cxx_value += matched ? matchers[i].rank : 0.0;
		}
		if (m_langs & LANG_OBJC)
		{
			objc_total += 1.0;
			objc_value += matched ? matchers[i].rank : 0.0;
		}
		if (m_langs & LANG_OBJCXX)
		{
			objcxx_total += 1.0;
			objcxx_value = matched ? matchers[i].rank : 0.0;
		}
	}

	geany_debug("Detection summary:\n"
	            "  C      : %f of %f (%f%%)\n"
	            "  C++    : %f of %f (%f%%)\n"
	            "  Obj-C  : %f of %f (%f%%)\n"
	            "  Obj-C++: %f of %f (%f%%)\n"
	            "----------------------------------",
	            c_value,      c_total,      c_value      / c_total      * 100.0,
	            cxx_value,    cxx_total,    cxx_value    / cxx_total    * 100.0,
	            objc_value,   objc_total,   objc_value   / objc_total   * 100.0,
	            objcxx_value, objcxx_total, objcxx_value / objcxx_total * 100.0);

	GeanyFiletype *likely_ft = ft;
	gdouble max_val = 0.0;

#define UPDATE_MAX(lang, ft) do {                                   \
	gdouble lang ## _avg = lang ## _value / lang ## _total;         \
	if (lang ## _avg > max_val) {                                   \
		max_val = lang ## _avg;                                     \
		likely_ft = filetypes_array->pdata[GEANY_FILETYPES_ ## ft]; \
	} } while (0)

	UPDATE_MAX(c, C);
	UPDATE_MAX(cxx, CPP);
	UPDATE_MAX(objc, OBJECTIVEC);
	UPDATE_MAX(objcxx, OBJECTIVEC);

	if (max_val == 0.0)
		likely_ft = ft;

	return likely_ft;
}

static void handle_document_signal(GObject *unused, GeanyDocument *doc,
	gpointer user_data)
{
	const char *filename = doc->real_path;
	char *base_name = g_path_get_basename(filename);
	bool is_hfile = (g_str_has_suffix(base_name, ".h") || !strchr(base_name, '.'));
	g_free(base_name);
	// Skip unless ends with .h or hasn't extension
	if (!is_hfile)
		return;
	// If something better was detected, use it
	GeanyFiletype *ft = detect_filetype(doc);
	if (ft != NULL && ft != doc->file_type)
		document_set_filetype(doc, ft);
}

#define CONNECT(name, func, data) \
	plugin_signal_connect(geany_plugin, NULL, name, TRUE, G_CALLBACK(func), data)

void plugin_init(GeanyData *data)
{
	init_regexp();
	CONNECT("document-new", handle_document_signal, NULL);
	CONNECT("document-open", handle_document_signal, NULL);
	CONNECT("document-reload", handle_document_signal, NULL);
}

void plugin_cleanup(void)
{
	deinit_regexp();
}
