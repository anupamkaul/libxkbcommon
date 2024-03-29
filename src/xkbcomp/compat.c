/************************************************************
 * Copyright (c) 1994 by Silicon Graphics Computer Systems, Inc.
 *
 * Permission to use, copy, modify, and distribute this
 * software and its documentation for any purpose and without
 * fee is hereby granted, provided that the above copyright
 * notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting
 * documentation, and that the name of Silicon Graphics not be
 * used in advertising or publicity pertaining to distribution
 * of the software without specific prior written permission.
 * Silicon Graphics makes no representation about the suitability
 * of this software for any purpose. It is provided "as is"
 * without any express or implied warranty.
 *
 * SILICON GRAPHICS DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT SHALL SILICON
 * GRAPHICS BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION  WITH
 * THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 ********************************************************/

/*
 * Copyright © 2012 Ran Benita <ran234@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "xkbcomp-priv.h"
#include "text.h"
#include "expr.h"
#include "action.h"
#include "vmod.h"
#include "include.h"

/*
 * The xkb_compat section
 * =====================
 * This section is the third to be processed, after xkb_keycodes and
 * xkb_types.
 *
 * Interpret statements
 * --------------------
 * Statements of the form:
 *      interpret Num_Lock+Any { ... }
 *      interpret Shift_Lock+AnyOf(Shift+Lock) { ... }
 *
 * The xkb_symbols section (see symbols.c) allows the keymap author to do,
 * among other things, the following for each key:
 * - Bind an action, like SetMods or LockGroup, to the key. Actions, like
 *   symbols, are specified for each level of each group in the key
 *   separately.
 * - Add a virtual modifier to the key's virtual modifier mapping (vmodmap).
 * - Specify whether the key should repeat or not.
 *
 * However, doing this for each key (or level) is tedious and inflexible.
 * Interpret's are a mechanism to apply these settings to a bunch of
 * keys/levels at once.
 *
 * Each interpret specifies a condition by which it attaches to certain
 * levels. The condition consists of two parts:
 * - A keysym. If the level has a different (or more than one) keysym, the
 *   match failes. Leaving out the keysym is equivalent to using the
 *   NoSymbol keysym, which always matches successfully.
 * - A modifier predicate. The predicate consists of a matching operation
 *   and a mask of (real) modifiers. The modifers are matched against the
 *   key's modifier map (modmap). The matching operation can be one of the
 *   following:
 *   + AnyOfOrNone - The modmap must either be empty or include at least
 *     one of the specified modifiers.
 *   + AnyOf - The modmap must include at least one of the specified
 *     modifiers.
 *   + NoneOf - The modmap must not include any of the specified modifiers.
 *   + AllOf - The modmap must include all of the specified modifiers (but
 *     may include others as well).
 *   + Exactly - The modmap must be exactly the same as the specified
 *     modifiers.
 *   Leaving out the predicate is equivalent to usign AnyOfOrNone while
 *   specifying all modifiers. Leaving out just the matching condtition
 *   is equivalent to using Exactly.
 * An interpret may also include "useModMapMods = level1;" - see below.
 *
 * If a level fulfils the conditions of several interpret's, only the
 * most specific one is used:
 * - A specific keysym will always match before a generic NoSymbol
 *   condition.
 * - If the keysyms are the same, the interpret with the more specific
 *   matching operation is used. The above list is sorted from least to
 *   most specific.
 * - If both the keysyms and the matching operations are the same (but the
 *   modifiers are different), the first interpret is used.
 *
 * As described above, once an interpret "attaches" to a level, it can bind
 * an action to that level, add one virtual modifier to the key's vmodmap,
 * or set the key's repeat setting. You should note the following:
 * - The key repeat is a property of the entire key; it is not level-specific.
 *   In order to avoid confusion, it is only inspected for the first level of
 *   the first group; the interpret's repeat setting is ignored when applied
 *   to other levels.
 * - If one of the above fields was set directly for a key in xkb_symbols,
 *   the explicit setting takes precedence over the interpret.
 *
 * The body of the statment may include statements of the following
 * forms (all of which are optional):
 *
 * - useModMapMods statement:
 *      useModMapMods = level1;
 *
 *   When set to 'level1', the interpret will only match levels which are
 *   the first level of the first group of the keys. This can be useful in
 *   conjuction with e.g. a virtualModifier statement.
 *
 * - action statement:
 *      action = LockMods(modifiers=NumLock);
 *
 *   Bind this action to the matching levels.
 *
 * - virtual modifier statement:
 *      virtualModifier = NumLock;
 *
 *   Add this virtual modifier to the key's vmodmap. The given virtual
 *   modifier must be declared at the top level of the file with a
 *   virtual_modifiers statement, e.g.:
 *      virtual_modifiers NumLock;
 *
 * - repeat statement:
 *      repeat = True;
 *
 *   Set whether the key should repeat or not. Must be a boolean value.
 *
 * Indicator map statements
 * ------------------------
 * Statements of the form:
 *      indicator "Shift Lock" { ... }
 *
 *   This statement specifies the behavior and binding of the indicator
 *   with the given name ("Shift Lock" above). The name should have been
 *   declared previously in the xkb_keycodes section (see Indicator name
 *   statement), and given an index there. If it wasn't, it is created
 *   with the next free index.
 *   The body of the statement describes the conditions of the keyboard
 *   state which will cause the indicator to be lit. It may include the
 *   following statements:
 *
 * - modifiers statment:
 *      modifiers = ScrollLock;
 *
 *   If the given modifiers are in the required state (see below), the
 *   led is lit.
 *
 * - whichModifierState statment:
 *      whichModState = Latched + Locked;
 *
 *   Can be any combination of:
 *      base, latched, locked, effective
 *      any (i.e. all of the above)
 *      none (i.e. none of the above)
 *      compat (legacy value, treated as effective)
 *   This will cause the respective portion of the modifer state (see
 *   struct xkb_state) to be matched against the modifiers given in the
 *   "modifiers" statement.
 *
 *   Here's a simple example:
 *      indicator "Num Lock" {
 *          modifiers = NumLock;
 *          whichModState = Locked;
 *      };
 *   Whenever the NumLock modifier is locked, the Num Lock indicator
 *   will light up.
 *
 * - groups statment:
 *      groups = All - group1;
 *
 *   If the given groups are in the required state (see below), the led
 *   is lit.
 *
 * - whichGroupState statment:
 *      whichGroupState = Effective;
 *
 *   Can be any combination of:
 *      base, latched, locked, effective
 *      any (i.e. all of the above)
 *      none (i.e. none of the above)
 *   This will cause the respective portion of the group state (see
 *   struct xkb_state) to be matched against the groups given in the
 *   "groups" statement.
 *
 *   Note: the above conditions are disjunctive, i.e. if any of them are
 *   satisfied the led is lit.
 *
 * Virtual modifier statements
 * ---------------------------
 * Statements of the form:
 *     virtual_modifiers LControl;
 *
 * Can appear in the xkb_types, xkb_compat, xkb_symbols sections.
 * TODO
 *
 * Effect on keymap
 * ----------------
 * After all of the xkb_compat sections have been compiled, the following
 * members of struct xkb_keymap are finalized:
 *      darray(struct xkb_sym_interpret) sym_interprets;
 *      darray(struct xkb_indicator_map) indicators;
 *      char *compat_section_name;
 * TODO: virtual modifiers.
 */

enum si_field {
    SI_FIELD_VIRTUAL_MOD    = (1 << 0),
    SI_FIELD_ACTION         = (1 << 1),
    SI_FIELD_AUTO_REPEAT    = (1 << 2),
    SI_FIELD_LEVEL_ONE_ONLY = (1 << 3),
};

typedef struct {
    enum si_field defined;
    unsigned file_id;
    enum merge_mode merge;

    struct xkb_sym_interpret interp;
} SymInterpInfo;

enum led_field {
    LED_FIELD_MODS       = (1 << 0),
    LED_FIELD_GROUPS     = (1 << 1),
    LED_FIELD_CTRLS      = (1 << 2),
};

typedef struct {
    enum led_field defined;
    unsigned file_id;
    enum merge_mode merge;

    struct xkb_indicator_map im;
} LEDInfo;

typedef struct {
    char *name;
    unsigned file_id;
    int errorCount;
    SymInterpInfo dflt;
    darray(SymInterpInfo) interps;
    LEDInfo ledDflt;
    darray(LEDInfo) leds;
    ActionsInfo *actions;
    struct xkb_keymap *keymap;
} CompatInfo;

static const char *
siText(SymInterpInfo *si, CompatInfo *info)
{
    char *buf = xkb_context_get_buffer(info->keymap->ctx, 128);

    if (si == &info->dflt)
        return "default";

    snprintf(buf, 128, "%s+%s(%s)",
             KeysymText(info->keymap->ctx, si->interp.sym),
             SIMatchText(si->interp.match),
             ModMaskText(info->keymap, si->interp.mods));

    return buf;
}

static inline bool
ReportSINotArray(CompatInfo *info, SymInterpInfo *si, const char *field)
{
    return ReportNotArray(info->keymap, "symbol interpretation", field,
                          siText(si, info));
}

static inline bool
ReportSIBadType(CompatInfo *info, SymInterpInfo *si, const char *field,
                const char *wanted)
{
    return ReportBadType(info->keymap->ctx, "symbol interpretation", field,
                         siText(si, info), wanted);
}

static inline bool
ReportIndicatorBadType(CompatInfo *info, LEDInfo *led,
                       const char *field, const char *wanted)
{
    return ReportBadType(info->keymap->ctx, "indicator map", field,
                         xkb_atom_text(info->keymap->ctx, led->im.name),
                         wanted);
}

static inline bool
ReportIndicatorNotArray(CompatInfo *info, LEDInfo *led,
                        const char *field)
{
    return ReportNotArray(info->keymap, "indicator map", field,
                          xkb_atom_text(info->keymap->ctx, led->im.name));
}

static void
InitCompatInfo(CompatInfo *info, struct xkb_keymap *keymap, unsigned file_id,
               ActionsInfo *actions)
{
    memset(info, 0, sizeof(*info));
    info->keymap = keymap;
    info->file_id = file_id;
    info->actions = actions;
    info->dflt.file_id = file_id;
    info->dflt.merge = MERGE_OVERRIDE;
    info->dflt.interp.virtual_mod = XKB_MOD_INVALID;
    info->ledDflt.file_id = file_id;
    info->ledDflt.merge = MERGE_OVERRIDE;
}

static void
ClearCompatInfo(CompatInfo *info)
{
    free(info->name);
    darray_free(info->interps);
    darray_free(info->leds);
}

static SymInterpInfo *
FindMatchingInterp(CompatInfo *info, SymInterpInfo *new)
{
    SymInterpInfo *old;

    darray_foreach(old, info->interps)
        if (old->interp.sym == new->interp.sym &&
            old->interp.mods == new->interp.mods &&
            old->interp.match == new->interp.match)
            return old;

    return NULL;
}

static bool
UseNewInterpField(enum si_field field, SymInterpInfo *old, SymInterpInfo *new,
                  bool report, enum si_field *collide)
{
    if (!(old->defined & field))
        return true;

    if (new->defined & field) {
        if (report)
            *collide |= field;

        if (new->merge != MERGE_AUGMENT)
            return true;
    }

    return false;
}

static bool
AddInterp(CompatInfo *info, SymInterpInfo *new)
{
    enum si_field collide = 0;
    SymInterpInfo *old;

    old = FindMatchingInterp(info, new);
    if (old) {
        int verbosity = xkb_context_get_log_verbosity(info->keymap->ctx);
        bool report = ((old->file_id == new->file_id && verbosity > 0) ||
                       verbosity > 9);

        if (new->merge == MERGE_REPLACE) {
            if (report)
                log_warn(info->keymap->ctx,
                         "Multiple definitions for \"%s\"; "
                         "Earlier interpretation ignored\n",
                         siText(new, info));
            *old = *new;
            return true;
        }

        if (UseNewInterpField(SI_FIELD_VIRTUAL_MOD, old, new, report,
                              &collide)) {
            old->interp.virtual_mod = new->interp.virtual_mod;
            old->defined |= SI_FIELD_VIRTUAL_MOD;
        }
        if (UseNewInterpField(SI_FIELD_ACTION, old, new, report,
                              &collide)) {
            old->interp.action = new->interp.action;
            old->defined |= SI_FIELD_ACTION;
        }
        if (UseNewInterpField(SI_FIELD_AUTO_REPEAT, old, new, report,
                              &collide)) {
            old->interp.repeat = new->interp.repeat;
            old->defined |= SI_FIELD_AUTO_REPEAT;
        }
        if (UseNewInterpField(SI_FIELD_LEVEL_ONE_ONLY, old, new, report,
                              &collide)) {
            old->interp.level_one_only = new->interp.level_one_only;
            old->defined |= SI_FIELD_LEVEL_ONE_ONLY;
        }

        if (collide) {
            log_warn(info->keymap->ctx,
                     "Multiple interpretations of \"%s\"; "
                     "Using %s definition for duplicate fields\n",
                     siText(new, info),
                     (new->merge != MERGE_AUGMENT ? "last" : "first"));
        }

        return true;
    }

    darray_append(info->interps, *new);
    return true;
}


/***====================================================================***/

static bool
ResolveStateAndPredicate(ExprDef *expr, enum xkb_match_operation *pred_rtrn,
                         xkb_mod_mask_t *mods_rtrn, CompatInfo *info)
{
    if (expr == NULL) {
        *pred_rtrn = MATCH_ANY_OR_NONE;
        *mods_rtrn = MOD_REAL_MASK_ALL;
        return true;
    }

    *pred_rtrn = MATCH_EXACTLY;
    if (expr->op == EXPR_ACTION_DECL) {
        const char *pred_txt = xkb_atom_text(info->keymap->ctx,
                                             expr->value.action.name);
        if (!LookupString(symInterpretMatchMaskNames, pred_txt, pred_rtrn)) {
            log_err(info->keymap->ctx,
                    "Illegal modifier predicate \"%s\"; Ignored\n", pred_txt);
            return false;
        }
        expr = expr->value.action.args;
    }
    else if (expr->op == EXPR_IDENT) {
        const char *pred_txt = xkb_atom_text(info->keymap->ctx,
                                             expr->value.str);
        if (pred_txt && istreq(pred_txt, "any")) {
            *pred_rtrn = MATCH_ANY;
            *mods_rtrn = MOD_REAL_MASK_ALL;
            return true;
        }
    }

    return ExprResolveModMask(info->keymap, expr, MOD_REAL, mods_rtrn);
}

/***====================================================================***/

static bool
UseNewLEDField(enum led_field field, LEDInfo *old, LEDInfo *new,
               bool report, enum led_field *collide)
{
    if (!(old->defined & field))
        return true;

    if (new->defined & field) {
        if (report)
            *collide |= field;

        if (new->merge != MERGE_AUGMENT)
            return true;
    }

    return false;
}

static bool
AddIndicatorMap(CompatInfo *info, LEDInfo *new)
{
    LEDInfo *old;
    enum led_field collide;
    struct xkb_context *ctx = info->keymap->ctx;
    int verbosity = xkb_context_get_log_verbosity(ctx);

    darray_foreach(old, info->leds) {
        bool report;

        if (old->im.name != new->im.name)
            continue;

        if (old->im.mods.mods == new->im.mods.mods &&
            old->im.groups == new->im.groups &&
            old->im.ctrls == new->im.ctrls &&
            old->im.which_mods == new->im.which_mods &&
            old->im.which_groups == new->im.which_groups) {
            old->defined |= new->defined;
            return true;
        }

        report = ((old->file_id == new->file_id && verbosity > 0) ||
                  verbosity > 9);

        if (new->merge == MERGE_REPLACE) {
            if (report)
                log_warn(info->keymap->ctx,
                         "Map for indicator %s redefined; "
                         "Earlier definition ignored\n",
                         xkb_atom_text(ctx, old->im.name));
            *old = *new;
            return true;
        }

        collide = 0;
        if (UseNewLEDField(LED_FIELD_MODS, old, new, report, &collide)) {
            old->im.which_mods = new->im.which_mods;
            old->im.mods = new->im.mods;
            old->defined |= LED_FIELD_MODS;
        }
        if (UseNewLEDField(LED_FIELD_GROUPS, old, new, report, &collide)) {
            old->im.which_groups = new->im.which_groups;
            old->im.groups = new->im.groups;
            old->defined |= LED_FIELD_GROUPS;
        }
        if (UseNewLEDField(LED_FIELD_CTRLS, old, new, report, &collide)) {
            old->im.ctrls = new->im.ctrls;
            old->defined |= LED_FIELD_CTRLS;
        }

        if (collide) {
            log_warn(info->keymap->ctx,
                     "Map for indicator %s redefined; "
                     "Using %s definition for duplicate fields\n",
                     xkb_atom_text(ctx, old->im.name),
                     (new->merge == MERGE_AUGMENT ? "first" : "last"));
        }

        return true;
    }

    darray_append(info->leds, *new);
    return true;
}

static void
MergeIncludedCompatMaps(CompatInfo *into, CompatInfo *from,
                        enum merge_mode merge)
{
    SymInterpInfo *si;
    LEDInfo *led;

    if (from->errorCount > 0) {
        into->errorCount += from->errorCount;
        return;
    }

    if (into->name == NULL) {
        into->name = from->name;
        from->name = NULL;
    }

    darray_foreach(si, from->interps) {
        si->merge = (merge == MERGE_DEFAULT ? si->merge : merge);
        if (!AddInterp(into, si))
            into->errorCount++;
    }

    darray_foreach(led, from->leds) {
        led->merge = (merge == MERGE_DEFAULT ? led->merge : merge);
        if (!AddIndicatorMap(into, led))
            into->errorCount++;
    }
}

static void
HandleCompatMapFile(CompatInfo *info, XkbFile *file, enum merge_mode merge);

static bool
HandleIncludeCompatMap(CompatInfo *info, IncludeStmt *stmt)
{
    enum merge_mode merge = MERGE_DEFAULT;
    XkbFile *rtrn;
    CompatInfo included, next_incl;

    InitCompatInfo(&included, info->keymap, info->file_id, info->actions);
    if (stmt->stmt) {
        free(included.name);
        included.name = stmt->stmt;
        stmt->stmt = NULL;
    }

    for (; stmt; stmt = stmt->next_incl) {
        if (!ProcessIncludeFile(info->keymap->ctx, stmt, FILE_TYPE_COMPAT,
                                &rtrn, &merge)) {
            info->errorCount += 10;
            ClearCompatInfo(&included);
            return false;
        }

        InitCompatInfo(&next_incl, info->keymap, rtrn->id, info->actions);
        next_incl.file_id = rtrn->id;
        next_incl.dflt = info->dflt;
        next_incl.dflt.file_id = rtrn->id;
        next_incl.dflt.merge = merge;
        next_incl.ledDflt.file_id = rtrn->id;
        next_incl.ledDflt.merge = merge;

        HandleCompatMapFile(&next_incl, rtrn, MERGE_OVERRIDE);

        MergeIncludedCompatMaps(&included, &next_incl, merge);

        ClearCompatInfo(&next_incl);
        FreeXkbFile(rtrn);
    }

    MergeIncludedCompatMaps(info, &included, merge);
    ClearCompatInfo(&included);

    return (info->errorCount == 0);
}

static bool
SetInterpField(CompatInfo *info, SymInterpInfo *si, const char *field,
               ExprDef *arrayNdx, ExprDef *value)
{
    struct xkb_keymap *keymap = info->keymap;
    xkb_mod_index_t ndx;

    if (istreq(field, "action")) {
        if (arrayNdx)
            return ReportSINotArray(info, si, field);

        if (!HandleActionDef(value, keymap, &si->interp.action, info->actions))
            return false;

        si->defined |= SI_FIELD_ACTION;
    }
    else if (istreq(field, "virtualmodifier") ||
             istreq(field, "virtualmod")) {
        if (arrayNdx)
            return ReportSINotArray(info, si, field);

        if (!ExprResolveMod(keymap, value, MOD_VIRT, &ndx))
            return ReportSIBadType(info, si, field, "virtual modifier");

        si->interp.virtual_mod = ndx;
        si->defined |= SI_FIELD_VIRTUAL_MOD;
    }
    else if (istreq(field, "repeat")) {
        bool set;

        if (arrayNdx)
            return ReportSINotArray(info, si, field);

        if (!ExprResolveBoolean(keymap->ctx, value, &set))
            return ReportSIBadType(info, si, field, "boolean");

        si->interp.repeat = set;

        si->defined |= SI_FIELD_AUTO_REPEAT;
    }
    else if (istreq(field, "locking")) {
        log_dbg(info->keymap->ctx,
                "The \"locking\" field in symbol interpretation is unsupported; "
                "Ignored\n");
    }
    else if (istreq(field, "usemodmap") ||
             istreq(field, "usemodmapmods")) {
        unsigned int val;

        if (arrayNdx)
            return ReportSINotArray(info, si, field);

        if (!ExprResolveEnum(keymap->ctx, value, &val, useModMapValueNames))
            return ReportSIBadType(info, si, field, "level specification");

        si->interp.level_one_only = !!val;
        si->defined |= SI_FIELD_LEVEL_ONE_ONLY;
    }
    else {
        return ReportBadField(keymap, "symbol interpretation", field,
                              siText(si, info));
    }

    return true;
}

static bool
SetIndicatorMapField(CompatInfo *info, LEDInfo *led,
                     const char *field, ExprDef *arrayNdx, ExprDef *value)
{
    bool ok = true;
    struct xkb_keymap *keymap = info->keymap;

    if (istreq(field, "modifiers") || istreq(field, "mods")) {
        if (arrayNdx)
            return ReportIndicatorNotArray(info, led, field);

        if (!ExprResolveModMask(keymap, value, MOD_BOTH, &led->im.mods.mods))
            return ReportIndicatorBadType(info, led, field, "modifier mask");

        led->defined |= LED_FIELD_MODS;
    }
    else if (istreq(field, "groups")) {
        unsigned int mask;

        if (arrayNdx)
            return ReportIndicatorNotArray(info, led, field);

        if (!ExprResolveMask(keymap->ctx, value, &mask, groupMaskNames))
            return ReportIndicatorBadType(info, led, field, "group mask");

        led->im.groups = mask;
        led->defined |= LED_FIELD_GROUPS;
    }
    else if (istreq(field, "controls") || istreq(field, "ctrls")) {
        unsigned int mask;

        if (arrayNdx)
            return ReportIndicatorNotArray(info, led, field);

        if (!ExprResolveMask(keymap->ctx, value, &mask, ctrlMaskNames))
            return ReportIndicatorBadType(info, led, field,
                                          "controls mask");

        led->im.ctrls = mask;
        led->defined |= LED_FIELD_CTRLS;
    }
    else if (istreq(field, "allowexplicit")) {
        log_dbg(info->keymap->ctx,
                "The \"allowExplicit\" field in indicator statements is unsupported; "
                "Ignored\n");
    }
    else if (istreq(field, "whichmodstate") ||
             istreq(field, "whichmodifierstate")) {
        unsigned int mask;

        if (arrayNdx)
            return ReportIndicatorNotArray(info, led, field);

        if (!ExprResolveMask(keymap->ctx, value, &mask,
                             modComponentMaskNames))
            return ReportIndicatorBadType(info, led, field,
                                          "mask of modifier state components");

        led->im.which_mods = mask;
    }
    else if (istreq(field, "whichgroupstate")) {
        unsigned mask;

        if (arrayNdx)
            return ReportIndicatorNotArray(info, led, field);

        if (!ExprResolveMask(keymap->ctx, value, &mask,
                             groupComponentMaskNames))
            return ReportIndicatorBadType(info, led, field,
                                          "mask of group state components");

        led->im.which_groups = mask;
    }
    else if (istreq(field, "driveskbd") ||
             istreq(field, "driveskeyboard") ||
             istreq(field, "leddriveskbd") ||
             istreq(field, "leddriveskeyboard") ||
             istreq(field, "indicatordriveskbd") ||
             istreq(field, "indicatordriveskeyboard")) {
        log_dbg(info->keymap->ctx,
                "The \"%s\" field in indicator statements is unsupported; "
                "Ignored\n", field);
    }
    else if (istreq(field, "index")) {
        /* Users should see this, it might cause unexpected behavior. */
        log_err(info->keymap->ctx,
                "The \"index\" field in indicator statements is unsupported; "
                "Ignored\n");
    }
    else {
        log_err(info->keymap->ctx,
                "Unknown field %s in map for %s indicator; "
                "Definition ignored\n",
                field, xkb_atom_text(keymap->ctx, led->im.name));
        ok = false;
    }

    return ok;
}

static bool
HandleGlobalVar(CompatInfo *info, VarDef *stmt)
{
    const char *elem, *field;
    ExprDef *ndx;
    bool ret;

    if (!ExprResolveLhs(info->keymap->ctx, stmt->name, &elem, &field, &ndx))
        ret = false;
    else if (elem && istreq(elem, "interpret"))
        ret = SetInterpField(info, &info->dflt, field, ndx, stmt->value);
    else if (elem && istreq(elem, "indicator"))
        ret = SetIndicatorMapField(info, &info->ledDflt, field, ndx,
                                   stmt->value);
    else
        ret = SetActionField(info->keymap, elem, field, ndx, stmt->value,
                             info->actions);
    return ret;
}

static bool
HandleInterpBody(CompatInfo *info, VarDef *def, SymInterpInfo *si)
{
    bool ok = true;
    const char *elem, *field;
    ExprDef *arrayNdx;

    for (; def; def = (VarDef *) def->common.next) {
        if (def->name && def->name->op == EXPR_FIELD_REF) {
            log_err(info->keymap->ctx,
                    "Cannot set a global default value from within an interpret statement; "
                    "Move statements to the global file scope\n");
            ok = false;
            continue;
        }

        ok = ExprResolveLhs(info->keymap->ctx, def->name, &elem, &field,
                            &arrayNdx);
        if (!ok)
            continue;

        ok = SetInterpField(info, si, field, arrayNdx, def->value);
    }

    return ok;
}

static bool
HandleInterpDef(CompatInfo *info, InterpDef *def, enum merge_mode merge)
{
    enum xkb_match_operation pred;
    xkb_mod_mask_t mods;
    SymInterpInfo si;

    if (!ResolveStateAndPredicate(def->match, &pred, &mods, info)) {
        log_err(info->keymap->ctx,
                "Couldn't determine matching modifiers; "
                "Symbol interpretation ignored\n");
        return false;
    }

    si = info->dflt;
    si.merge = merge = (def->merge == MERGE_DEFAULT ? merge : def->merge);

    if (!LookupKeysym(def->sym, &si.interp.sym)) {
        log_err(info->keymap->ctx,
                "Could not resolve keysym %s; "
                "Symbol interpretation ignored\n",
                def->sym);
        return false;
    }

    si.interp.match = pred;
    si.interp.mods = mods;

    if (!HandleInterpBody(info, def->def, &si)) {
        info->errorCount++;
        return false;
    }

    if (!AddInterp(info, &si)) {
        info->errorCount++;
        return false;
    }

    return true;
}

static bool
HandleIndicatorMapDef(CompatInfo *info, IndicatorMapDef *def,
                      enum merge_mode merge)
{
    LEDInfo led;
    VarDef *var;
    bool ok;

    if (def->merge != MERGE_DEFAULT)
        merge = def->merge;

    led = info->ledDflt;
    led.merge = merge;
    led.im.name = def->name;

    ok = true;
    for (var = def->body; var != NULL; var = (VarDef *) var->common.next) {
        const char *elem, *field;
        ExprDef *arrayNdx;
        if (!ExprResolveLhs(info->keymap->ctx, var->name, &elem, &field,
                            &arrayNdx)) {
            ok = false;
            continue;
        }

        if (elem) {
            log_err(info->keymap->ctx,
                    "Cannot set defaults for \"%s\" element in indicator map; "
                    "Assignment to %s.%s ignored\n", elem, elem, field);
            ok = false;
        }
        else {
            ok = SetIndicatorMapField(info, &led, field, arrayNdx,
                                      var->value) && ok;
        }
    }

    if (ok)
        return AddIndicatorMap(info, &led);

    return false;
}

static void
HandleCompatMapFile(CompatInfo *info, XkbFile *file, enum merge_mode merge)
{
    bool ok;
    ParseCommon *stmt;

    merge = (merge == MERGE_DEFAULT ? MERGE_AUGMENT : merge);

    free(info->name);
    info->name = strdup_safe(file->name);

    for (stmt = file->defs; stmt; stmt = stmt->next) {
        switch (stmt->type) {
        case STMT_INCLUDE:
            ok = HandleIncludeCompatMap(info, (IncludeStmt *) stmt);
            break;
        case STMT_INTERP:
            ok = HandleInterpDef(info, (InterpDef *) stmt, merge);
            break;
        case STMT_GROUP_COMPAT:
            log_dbg(info->keymap->ctx,
                    "The \"group\" statement in compat is unsupported; "
                    "Ignored\n");
            ok = true;
            break;
        case STMT_INDICATOR_MAP:
            ok = HandleIndicatorMapDef(info, (IndicatorMapDef *) stmt, merge);
            break;
        case STMT_VAR:
            ok = HandleGlobalVar(info, (VarDef *) stmt);
            break;
        case STMT_VMOD:
            ok = HandleVModDef(info->keymap, (VModDef *) stmt);
            break;
        default:
            log_err(info->keymap->ctx,
                    "Interpretation files may not include other types; "
                    "Ignoring %s\n", stmt_type_to_string(stmt->type));
            ok = false;
            break;
        }

        if (!ok)
            info->errorCount++;

        if (info->errorCount > 10) {
            log_err(info->keymap->ctx,
                    "Abandoning compatibility map \"%s\"\n", file->topName);
            break;
        }
    }
}

static void
CopyInterps(CompatInfo *info, bool needSymbol, enum xkb_match_operation pred)
{
    SymInterpInfo *si;

    darray_foreach(si, info->interps)
        if (si->interp.match == pred &&
            (si->interp.sym != XKB_KEY_NoSymbol) == needSymbol)
            darray_append(info->keymap->sym_interprets, si->interp);
}

static void
CopyIndicatorMapDefs(CompatInfo *info)
{
    LEDInfo *led;
    xkb_led_index_t i;
    struct xkb_indicator_map *im;
    struct xkb_keymap *keymap = info->keymap;

    darray_foreach(led, info->leds) {
        /*
         * Find the indicator with the given name, if it was already
         * declared in keycodes.
         */
        darray_enumerate(i, im, keymap->indicators)
            if (im->name == led->im.name)
                break;

        /* Not previously declared; create it with next free index. */
        if (i >= darray_size(keymap->indicators)) {
            log_dbg(keymap->ctx,
                    "Indicator name \"%s\" was not declared in the keycodes section; "
                    "Adding new indicator\n",
                    xkb_atom_text(keymap->ctx, led->im.name));

            darray_enumerate(i, im, keymap->indicators)
                if (im->name == XKB_ATOM_NONE)
                    break;

            if (i >= darray_size(keymap->indicators)) {
                /* Not place to put it; ignore. */
                if (i >= XKB_MAX_LEDS) {
                    log_err(keymap->ctx,
                            "Too many indicators (maximum is %d); "
                            "Indicator name \"%s\" ignored\n",
                            XKB_MAX_LEDS,
                            xkb_atom_text(keymap->ctx, led->im.name));
                    continue;
                }
                /* Add a new indicator. */
                darray_resize(keymap->indicators, i + 1);
                im = &darray_item(keymap->indicators, i);
            }
        }

        *im = led->im;
        if (im->groups != 0 && im->which_groups == 0)
            im->which_groups = XKB_STATE_LAYOUT_EFFECTIVE;
        if (im->mods.mods != 0 && im->which_mods == 0)
            im->which_mods = XKB_STATE_MODS_EFFECTIVE;
    }
}

static bool
CopyCompatToKeymap(struct xkb_keymap *keymap, CompatInfo *info)
{
    keymap->compat_section_name = strdup_safe(info->name);

    if (!darray_empty(info->interps)) {
        /* Most specific to least specific. */
        CopyInterps(info, true, MATCH_EXACTLY);
        CopyInterps(info, true, MATCH_ALL);
        CopyInterps(info, true, MATCH_NONE);
        CopyInterps(info, true, MATCH_ANY);
        CopyInterps(info, true, MATCH_ANY_OR_NONE);
        CopyInterps(info, false, MATCH_EXACTLY);
        CopyInterps(info, false, MATCH_ALL);
        CopyInterps(info, false, MATCH_NONE);
        CopyInterps(info, false, MATCH_ANY);
        CopyInterps(info, false, MATCH_ANY_OR_NONE);
    }

    CopyIndicatorMapDefs(info);

    return true;
}

bool
CompileCompatMap(XkbFile *file, struct xkb_keymap *keymap,
                 enum merge_mode merge)
{
    CompatInfo info;
    ActionsInfo *actions;

    actions = NewActionsInfo();
    if (!actions)
        return false;

    InitCompatInfo(&info, keymap, file->id, actions);
    info.dflt.merge = merge;
    info.ledDflt.merge = merge;

    HandleCompatMapFile(&info, file, merge);
    if (info.errorCount != 0)
        goto err_info;

    if (!CopyCompatToKeymap(keymap, &info))
        goto err_info;

    ClearCompatInfo(&info);
    FreeActionsInfo(actions);
    return true;

err_info:
    ClearCompatInfo(&info);
    FreeActionsInfo(actions);
    return false;
}
