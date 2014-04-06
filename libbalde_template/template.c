/*
 * balde: A microframework for C based on GLib and bad intentions.
 * Copyright (C) 2013-2014 Rafael G. Martins <rafael@rafaelmartins.eng.br>
 *
 * This program can be distributed under the terms of the LGPL-2 License.
 * See the file COPYING.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <glib.h>
#include <stdlib.h>
#include <string.h>
#include <balde-template/template-private.h>
#include <balde-template/parser-private.h>


static gboolean
replace_template_variables_cb(const GMatchInfo *info, GString *res, gpointer data)
{
    gchar *match = g_match_info_fetch(info, 1);
    GSList **tmp = (GSList**) data;
    *tmp = g_slist_append(*tmp, (gchar*) g_strdup(match));
    data = tmp;
    g_string_append(res, "%s");
    g_free (match);
    return FALSE;
}


gchar*
balde_template_generate_source(const gchar *template_name,
    const gchar *template_source)
{
    GRegex *re_percent = g_regex_new("%", 0, 0, NULL);
    GSList *blocks = balde_template_parse(template_source);
    GSList *printf_args = NULL;
    GString *includes = g_string_new("");
    GString *parsed = g_string_new("");
    balde_template_block_t *node;
    balde_template_include_block_t *iblock;
    balde_template_content_block_t *cblock;
    balde_template_print_var_block_t *vblock;
    balde_template_print_fn_call_block_t *fblock;
    balde_template_fn_arg_t *arg;
    gchar *escaped_content;
    GString *fn_args = NULL;
    for (GSList *tmp = blocks; tmp != NULL; tmp = g_slist_next(tmp)) {
        node = tmp->data;
        switch (node->type) {
            case BALDE_TEMPLATE_INCLUDE_BLOCK:
                iblock = node->block;
                g_string_append_printf(includes, "#include <%s>\n", iblock->include);
                break;
            case BALDE_TEMPLATE_CONTENT_BLOCK:
                cblock = node->block;
                escaped_content = g_regex_replace_literal(re_percent, cblock->content,
                    -1, 0, "%%", 0, NULL);
                g_string_append(parsed, escaped_content);
                g_free(escaped_content);
                break;
            case BALDE_TEMPLATE_PRINT_VAR_BLOCK:
                vblock = node->block;
                g_string_append(parsed, "%s");
                printf_args = g_slist_append(printf_args, g_strdup_printf(
                    "        balde_response_get_tmpl_var(response, \"%s\")",
                    vblock->variable));
                break;
            case BALDE_TEMPLATE_PRINT_FN_CALL_BLOCK:
                fblock = node->block;
                g_string_append(parsed, "%s");
                fn_args = g_string_new("");
                g_string_append_printf(fn_args, "        %s(", fblock->name);
                if (fblock->args != NULL)
                    g_string_append(fn_args, "\n");
                else
                    g_string_append(fn_args, ")");
                for (GSList *tmp2 = fblock->args; tmp2 != NULL; tmp2 = g_slist_next(tmp2)) {
                    arg = tmp2->data;
                    switch (arg->type) {
                        case BALDE_TEMPLATE_FN_ARG_STRING:
                        case BALDE_TEMPLATE_FN_ARG_INT:
                        case BALDE_TEMPLATE_FN_ARG_FLOAT:
                        case BALDE_TEMPLATE_FN_ARG_BOOL:
                            g_string_append_printf(fn_args, "            %s", arg->content);
                            break;
                        case BALDE_TEMPLATE_FN_ARG_VAR:
                            g_string_append_printf(fn_args,
                                "            balde_response_get_tmpl_var(response, \"%s\")",
                                arg->content);
                            break;
                    }
                    if (g_slist_next(tmp2) == NULL)
                        g_string_append(fn_args, ")");
                    else
                        g_string_append(fn_args, ",\n");

                }
                printf_args = g_slist_append(printf_args, g_string_free(fn_args, FALSE));
                break;
        }
    }

    g_regex_unref(re_percent);
    balde_template_free_blocks(blocks);

    // escape newlines.
    gchar *parsed_tmp = g_string_free(parsed, FALSE);
    gchar *escaped = g_strescape(parsed_tmp, "");
    g_free(parsed_tmp);

    gchar *tmp_includes = g_string_free(includes, FALSE);

    GString *rv = g_string_new("");
    g_string_append_printf(rv,
        "// WARNING: this file was generated automatically by balde-template-gen\n"
        "\n"
        "#include <balde.h>\n"
        "#include <glib.h>\n"
        "%s\n"
        "static const gchar *balde_template_%s_format = \"%s\";\n"
        "extern void balde_template_%s(balde_response_t *response);\n"
        "\n"
        "void\n"
        "balde_template_%s(balde_response_t *response)\n"
        "{\n",
        tmp_includes, template_name, escaped, template_name, template_name);
    g_free(tmp_includes);

    if (printf_args == NULL) {
        g_string_append_printf(rv,
            "    gchar *rv = g_strdup(balde_template_%s_format);\n",
            template_name);
    }
    else {
        g_string_append_printf(rv,
            "    gchar *rv = g_strdup_printf(balde_template_%s_format,\n",
            template_name);
    }
    for (GSList *tmp = printf_args; tmp != NULL; tmp = tmp->next) {
        g_string_append(rv, (gchar*) tmp->data);
        if (tmp->next != NULL)
            g_string_append(rv, ",\n");
        else
            g_string_append(rv, ");\n");
    }
    g_slist_free_full(printf_args, g_free);
    g_string_append(rv,
        "    balde_response_append_body(response, rv);\n"
        "    g_free(rv);\n"
        "}\n");
    g_free(escaped);
    return g_string_free(rv, FALSE);
}


gchar*
balde_template_generate_header(const gchar *template_name)
{
    return g_strdup_printf(
        "// WARNING: this file was generated automatically by balde-template-gen\n"
        "\n"
        "#ifndef __%s_balde_template\n"
        "#define __%s_balde_template\n"
        "\n"
        "#include <balde.h>\n"
        "\n"
        "extern void balde_template_%s(balde_response_t *response);\n"
        "\n"
        "#endif\n", template_name, template_name, template_name);
}


gchar*
balde_template_get_name(const gchar *template_basename)
{
    gchar *template_name = g_path_get_basename(template_basename);
    for (guint i = strlen(template_name); i != 0; i--) {
        if (template_name[i] == '.') {
            template_name[i] = '\0';
            break;
        }
    }
    for (guint i = 0; template_name[i] != '\0'; i++) {
        if (!g_ascii_isalpha(template_name[i])) {
            template_name[i] = '_';
        }
    }
    return template_name;
}


int
balde_template_main(int argc, char **argv)
{
    int rv = EXIT_SUCCESS;
    if (argc != 3) {
        g_printerr("Usage: $ balde-template-gen template.html template.[ch]\n");
        rv = EXIT_FAILURE;
        goto point1;
    }
    gchar *template_name = balde_template_get_name(argv[2]);
    gchar *template_source = NULL;
    gchar *source = NULL;
    if (g_str_has_suffix(argv[2], ".c")) {
        if (!g_file_get_contents(argv[1], &template_source, NULL, NULL)) {
            g_printerr("Failed to read source file: %s\n", argv[1]);
            rv = EXIT_FAILURE;
            goto point2;
        }
        source = balde_template_generate_source(template_name, template_source);
    }
    else if (g_str_has_suffix(argv[2], ".h")) {
        source = balde_template_generate_header(template_name);
    }
    else {
        g_printerr("Invalid filename: %s\n", argv[2]);
        rv = EXIT_FAILURE;
        goto point3;
    }
    if (!g_file_set_contents(argv[2], source, -1, NULL)) {
        g_printerr("Failed to write file: %s\n", argv[2]);
        rv = EXIT_FAILURE;
        goto point3;  // duh!
    }
point3:
    g_free(source);
    g_free(template_source);
point2:
    g_free(template_name);
point1:
    return rv;
}
