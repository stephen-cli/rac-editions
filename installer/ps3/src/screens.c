#include "screens.h"

#include <stdio.h>
#include <string.h>

#include "ui.h"
#include "update.h"
#include "words.h"

#define MARGIN 72
#define CONTENT_W (SCREEN_W - 2 * MARGIN)
#define FOOTER_Y 652
/* how long a first Circle stays ready to cancel the build: 3 seconds, by the clock, as frames come at any rate */
#define CANCEL_WINDOW 3000

static const char *const STEP_NAMES[] = {"Edition", "Discs", "Review", "Build"};

void screens_open(Screens *screens)
{
    memset(screens, 0, sizeof *screens);
}

static int accept_glyph(void)
{
    return platform_accept_is_cross() ? GLYPH_CROSS : GLYPH_CIRCLE;
}

static int back_glyph(void)
{
    return platform_accept_is_cross() ? GLYPH_CIRCLE : GLYPH_CROSS;
}

/* "4.5 GB" */
static void gigabytes(uint64_t bytes, char *text, size_t capacity)
{
    snprintf(text, capacity, "%.1f GB", (double)bytes / 1e9);
}

/* The bar across the top: the builder's name, the edition's version and the steps, the current one gold. A
   release's version is its tag ("v0.2.0"); between releases it is a commit, shortened. */
static void header(const Session *session, int step)
{
    ui_text(FONT_LABEL, MARGIN, 34, COLOUR_HOLO, "EDITIONS BUILDER");
    if (session->version[0] != 0) {
        char version[64];
        bool commit = strlen(session->version) == 40 && strspn(session->version, "0123456789abcdef") == 40;
        snprintf(version, sizeof version, "executive edition %.*s", commit ? 7 : 40, session->version);
        ui_text(FONT_SMALL, SCREEN_W - MARGIN - ui_text_width(FONT_SMALL, version), 32, COLOUR_INK_FAINT, version);
    }
    int x = MARGIN;
    for (int i = 0; i < 4; i++) {
        char label[32];
        snprintf(label, sizeof label, "%d  %s", i + 1, STEP_NAMES[i]);
        Colour colour = i == step ? COLOUR_BOLT : i < step ? COLOUR_INK_DIM : COLOUR_INK_FAINT;
        x += ui_text(FONT_LABEL, x, 66, colour, label) + 40;
    }
    ui_panel(MARGIN, 96, CONTENT_W, 1, 0, COLOUR_EDGE_SOFT, (Colour){0, 0, 0, 0});
}

static void title(const char *text, Colour colour)
{
    ui_text(FONT_TITLE, MARGIN, 112, colour, text);
}

/* The prompts along the bottom, right to left from the right margin: GLYPHS[i] with LABELS[i]. */
static void footer(const int *glyphs, const char *const *labels, int count)
{
    int x = SCREEN_W - MARGIN;
    for (int i = count - 1; i >= 0; i--) {
        int width = 30 + 10 + ui_text_width(FONT_BODY, labels[i]);
        x -= width;
        ui_prompt(x, FOOTER_Y, glyphs[i], labels[i]);
        x -= 36;
    }
}

/* ---- Edition ---- */

static void edition_screen(Screens *screens, Session *session, unsigned pressed)
{
    header(session, 0);
    title(session->have_edition ? session->edition_name : "Executive Edition", COLOUR_INK);
    char line[160];
    snprintf(line, sizeof line, "%s, built from your own disc on this PS3. Nothing from the game is downloaded.",
             session->game[0] ? session->game : "Ratchet & Clank");
    ui_text(FONT_BODY, MARGIN, 172, COLOUR_INK_DIM, line);

    ui_panel(MARGIN, 214, CONTENT_W, 422, 14, COLOUR_PLATE, COLOUR_EDGE);
    ui_text(FONT_LABEL, MARGIN + 28, 230, COLOUR_HOLO, "WHAT'S IN IT");
    int column_w = (CONTENT_W - 28 * 3) / 2;
    for (size_t i = 0; i < FEATURE_COUNT; i++) {
        int column = (int)(i % 2), row = (int)(i / 2);
        int x = MARGIN + 28 + column * (column_w + 28), y = 260 + row * 74;
        ui_text(FONT_BODY_BOLD, x, y, COLOUR_INK, FEATURES[i].name);
        ui_wrap(FONT_SMALL, x, y + 27, column_w, COLOUR_INK_DIM, FEATURES[i].text);
    }
    /* a newer builder, once the update check has found one: Triangle downloads it */
    Update update;
    update_get(&update);
    char get[64];
    snprintf(get, sizeof get, "%s %s",
             update.state == UPDATE_DOWNLOADING ? "Downloading" : update.state == UPDATE_DOWNLOADED ? "Install" : "Get",
             update.latest);
    const int glyphs[] = {GLYPH_TRIANGLE, back_glyph(), accept_glyph()};
    const char *const labels[] = {get, "Quit", "Next: discs"};
    bool newer = update.state != UPDATE_NONE;
    footer(glyphs + (newer ? 0 : 1), labels + (newer ? 0 : 1), newer ? 3 : 2);

    if (pressed & PAD_ACCEPT && session->have_edition) {
        screens->screen = SCREEN_DISCS;
    } else if (pressed & PAD_BACK) {
        screens->quit = true;
    } else if (pressed & PAD_TRIANGLE && newer) {
        update_download();
        screens->screen = SCREEN_UPDATE;
    }
}

/* ---- Discs ---- */

static void disc_slot(const Session *session, int d, int x, int y, int w, int h)
{
    const EditionDisc *disc = &session->discs[d];
    bool found = disc->image >= 0;
    Colour edge = found ? COLOUR_CLANK : disc->required ? COLOUR_EDGE : COLOUR_EDGE_SOFT;
    ui_panel(x, y, w, h, 14, COLOUR_PLATE, edge);
    char label[96];
    session_disc_label(session, d, label, sizeof label);
    ui_text(FONT_HEADING, x + 24, y + 18, COLOUR_INK, label);
    ui_text(FONT_SMALL, x + 24, y + 56, COLOUR_INK_DIM, disc_purpose(disc->key));
    int line_y = y + 96;
    if (found) {
        ui_text(FONT_BODY_BOLD, x + 24, line_y, COLOUR_CLANK, "Found");
        ui_wrap(FONT_SMALL, x + 24, line_y + 30, w - 48, COLOUR_INK, session->images[disc->image].name);
        return;
    }
    /* still being looked for: the image being checked, if it might be this one */
    if (!session_discs_settled(session)) {
        ui_text(FONT_BODY, x + 24, line_y, COLOUR_INK_DIM, "Looking...");
        return;
    }
    if (disc->required) {
        char words[200];
        snprintf(words, sizeof words, "Not found. Copy your %s disc image into PS2ISO.", label);
        ui_wrap(FONT_BODY, x + 24, line_y, w - 48, COLOUR_HAZARD, words);
    } else {
        ui_wrap(FONT_BODY, x + 24, line_y, w - 48, COLOUR_INK_FAINT, "Not found. The edition is built without it.");
    }
}

static void discs_screen(Screens *screens, Session *session, unsigned pressed)
{
    header(session, 1);
    title("Your discs", COLOUR_INK);
    char where[600];
    snprintf(where, sizeof where, "The disc images in %s. Nothing leaves your PS3.", session->places.discs);
    ui_text(FONT_BODY, MARGIN, 172, COLOUR_INK_DIM, where);

    int slot_w = (CONTENT_W - 24) / 2;
    for (int d = 0; d < session->disc_count && d < 2; d++) {
        disc_slot(session, d, MARGIN + d * (slot_w + 24), 220, slot_w, 190);
    }

    /* what else is there: the image being checked, images refused and why, and how many others */
    int y = 434;
    int others = 0;
    for (int i = 0; i < session->image_count && y < 600; i++) {
        const Image *image = &session->images[i];
        if (image->state == IMAGE_CHECKING) {
            ui_text(FONT_BODY, MARGIN, y, COLOUR_INK, image->name);
            ui_bar(MARGIN, y + 32, CONTENT_W, 10, image->progress, COLOUR_HOLO);
            char percent[32];
            snprintf(percent, sizeof percent, "Checking %d%%", (int)(image->progress * 100));
            ui_text(FONT_SMALL, SCREEN_W - MARGIN - ui_text_width(FONT_SMALL, percent), y, COLOUR_HOLO, percent);
            y += 58;
        } else if (image->state == IMAGE_REFUSED) {
            ui_text(FONT_BODY, MARGIN, y, COLOUR_INK, image->name);
            y += ui_line_height(FONT_BODY);
            y += ui_wrap(FONT_SMALL, MARGIN, y, CONTENT_W, COLOUR_HAZARD, image->message) + 10;
        } else if (image->state == IMAGE_OTHER) {
            others++;
        }
    }
    if (others > 0 && y < 620) {
        char line[96];
        snprintf(line, sizeof line, others == 1 ? "%d other image in PS2ISO isn't used by this edition."
                                                : "%d other images in PS2ISO aren't used by this edition.", others);
        ui_text(FONT_SMALL, MARGIN, y, COLOUR_INK_FAINT, line);
    }
    if (session->scanned && session->image_count == 0) {
        ui_text(FONT_BODY, MARGIN, y, COLOUR_INK_DIM, "There are no disc images in PS2ISO yet.");
    }

    bool ready = session_discs_settled(session) && session->disc_count > 0 && session->discs[0].image >= 0;
    const int glyphs[] = {GLYPH_TRIANGLE, back_glyph(), accept_glyph()};
    const char *const labels[] = {"Look again", "Back", "Next: review"};
    footer(ready ? glyphs : glyphs, labels, ready ? 3 : 2);

    if (pressed & PAD_ACCEPT && ready) {
        screens->screen = SCREEN_REVIEW;
        screens->selected = 0;
        session_plan(session);
    } else if (pressed & PAD_BACK) {
        screens->screen = SCREEN_EDITION;
    } else if (pressed & PAD_TRIANGLE) {
        session_rescan(session);
    }
}

/* ---- Review and build ---- */

/* The review's rows: the economy (when there is a choice), each switch, then Build. */
static int review_rows(const Session *session)
{
    return (session->economy_count > 1 ? 1 : 0) + session->switch_count + 1;
}

static bool left_out(const Session *session, const char *id)
{
    for (int i = 0; i < session->left_out_count; i++) {
        if (strcmp(session->left_out[i], id) == 0) {
            return true;
        }
    }
    return false;
}

static void review_screen(Screens *screens, Session *session, unsigned pressed)
{
    header(session, 2);
    title("Review and build", COLOUR_INK);

    /* the new image */
    ui_panel(MARGIN, 176, CONTENT_W, 118, 14, COLOUR_PLATE, COLOUR_EDGE);
    ui_text(FONT_LABEL, MARGIN + 24, 190, COLOUR_HOLO, "THE NEW IMAGE");
    ui_text(FONT_BODY_BOLD, MARGIN + 24, 216, COLOUR_INK, session->output);
    char line[200];
    if (!session->planned) {
        snprintf(line, sizeof line, "Checking the space...");
        ui_text(FONT_SMALL, MARGIN + 24, 252, COLOUR_INK_DIM, line);
    } else if (session->plan_problem[0] != 0) {
        ui_text(FONT_SMALL, MARGIN + 24, 252, COLOUR_HAZARD, session->plan_problem);
    } else {
        char size[32];
        gigabytes(session->bytes_needed, size, sizeof size);
        snprintf(line, sizeof line, "Building needs about %s free on the hard drive.%s", size,
                 session->output_exists ? " It replaces the image built before." : "");
        ui_text(FONT_SMALL, MARGIN + 24, 252, COLOUR_INK_DIM, line);
    }

    /* the edition settings */
    ui_text(FONT_LABEL, MARGIN, 316, COLOUR_HOLO, "EDITION SETTINGS");
    ui_wrap(FONT_SMALL, MARGIN + 190, 314, CONTENT_W - 190, COLOUR_INK_FAINT, DESIGNED_NOTE);
    int rows = review_rows(session);
    if (screens->selected >= rows) {
        screens->selected = rows - 1;
    }
    int y = 370;
    int row = 0;
    if (session->economy_count > 1) {
        bool on_row = screens->selected == row;
        if (on_row) {
            ui_panel(MARGIN, y - 6, CONTENT_W, 44, 8, COLOUR_HOLO_GLASS, COLOUR_HOLO_LINE);
        }
        ui_text(FONT_BODY_BOLD, MARGIN + 18, y, COLOUR_INK, "Challenge Mode bolts");
        const Economy *economy = economy_named(session->economies[session->economy]);
        char value[200];
        snprintf(value, sizeof value, "<  %s  >   %s", economy != NULL ? economy->label : session->economies[session->economy],
                 economy != NULL ? economy->text : "");
        ui_text(FONT_SMALL, MARGIN + 300, y + 3, on_row ? COLOUR_HOLO : COLOUR_INK_DIM, value);
        y += 46;
        row++;
    }
    for (int i = 0; i < session->switch_count; i++, row++) {
        const Switch *entry = &session->switches[i];
        const Feature *feature = feature_for_switch(entry->id);
        bool available = session_switch_available(session, i);
        bool on_row = screens->selected == row;
        if (on_row) {
            ui_panel(MARGIN, y - 6, CONTENT_W, 44, 8, COLOUR_HOLO_GLASS, COLOUR_HOLO_LINE);
        }
        ui_text(FONT_BODY_BOLD, MARGIN + 18, y, available ? COLOUR_INK : COLOUR_INK_FAINT,
                feature != NULL ? feature->name : entry->id);
        const char *state = !available ? "Needs Going Commando (USA)" : entry->on ? "On" : "Off";
        Colour colour = !available ? COLOUR_INK_FAINT : entry->on ? COLOUR_CLANK : COLOUR_HAZARD;
        ui_text(FONT_BODY_BOLD, MARGIN + 300, y, colour, state);
        y += 46;
    }
    /* Build */
    bool can_build = session->planned && session->plan_problem[0] == 0;
    bool on_build = screens->selected == row;
    int button_w = 340;
    ui_panel(MARGIN, y + 8, button_w, 52, 10, can_build ? COLOUR_BOLT : COLOUR_EDGE_SOFT,
             on_build ? COLOUR_INK : (Colour){0, 0, 0, 0});
    char build_label[80];
    snprintf(build_label, sizeof build_label, "Build %s", session->edition_name);
    ui_text(FONT_HEADING, MARGIN + (button_w - ui_text_width(FONT_HEADING, build_label)) / 2, y + 17,
            can_build ? COLOUR_BOLT_INK : COLOUR_INK_FAINT, build_label);

    const int glyphs[] = {back_glyph(), accept_glyph()};
    const char *const labels[] = {"Back", screens->selected == row ? "Build" : "Change"};
    footer(glyphs, labels, 2);

    if (pressed & PAD_UP && screens->selected > 0) {
        screens->selected--;
    } else if (pressed & PAD_DOWN && screens->selected < rows - 1) {
        screens->selected++;
    } else if (pressed & PAD_BACK) {
        screens->screen = SCREEN_DISCS;
    } else if (pressed & (PAD_ACCEPT | PAD_LEFT | PAD_RIGHT)) {
        int at = screens->selected;
        if (session->economy_count > 1 && at == 0) {
            int step = pressed & PAD_LEFT ? session->economy_count - 1 : 1;
            session->economy = (session->economy + step) % session->economy_count;
            session_plan(session);
        } else if (at == rows - 1) {
            if (pressed & PAD_ACCEPT && can_build) {
                session_build(session);
                screens->screen = SCREEN_BUILD;
                screens->cancel_asked = 0;
            }
        } else {
            int i = at - (session->economy_count > 1 ? 1 : 0);
            if (session_switch_available(session, i)) {
                session->switches[i].on = !session->switches[i].on;
                session_plan(session);
            }
        }
    }
    (void)left_out;
}

/* ---- Build ---- */

static void build_screen(Screens *screens, Session *session, unsigned pressed)
{
    header(session, 3);
    if (session->build == BUILD_FAILED || session->build == BUILD_CANCELLED) {
        title(session->build == BUILD_CANCELLED ? "The build was cancelled" : "The build stopped", COLOUR_HAZARD);
        int y = 184;
        if (session->build == BUILD_CANCELLED) {
            ui_text(FONT_BODY, MARGIN, y, COLOUR_INK, "The unfinished image was deleted.");
        } else {
            y += ui_wrap(FONT_BODY, MARGIN, y, CONTENT_W, COLOUR_INK, session->failure) + 16;
            if (session->last_log[0] != 0) {
                ui_wrap(FONT_SMALL, MARGIN, y, CONTENT_W, COLOUR_INK_FAINT, session->last_log);
            }
        }
        const int glyphs[] = {accept_glyph()};
        const char *const labels[] = {"Back to the review"};
        footer(glyphs, labels, 1);
        if (pressed & PAD_ACCEPT) {
            session->build = BUILD_IDLE;
            screens->screen = SCREEN_REVIEW;
            session_plan(session);
        }
        return;
    }
    char heading[96];
    snprintf(heading, sizeof heading, "Building the %s", session->edition_name);
    title(heading, COLOUR_INK);
    ui_text(FONT_BODY, MARGIN, 172, COLOUR_INK_DIM, "Keep the PS3 on until it's done. It takes a while.");

    ui_panel(MARGIN, 220, CONTENT_W, 360, 14, COLOUR_PLATE, COLOUR_EDGE);
    int y = 244;
    for (int i = 0; i < session->row_count; i++) {
        const BuildRow *row = &session->rows[i];
        bool finished = i < session->row_count - 1 || session->build == BUILD_DONE;
        double fraction = finished ? 1 : row->total > 0 ? (double)row->done / (double)row->total : 0;
        ui_text(FONT_BODY_BOLD, MARGIN + 28, y, finished ? COLOUR_INK_DIM : COLOUR_INK, row->label);
        char percent[16];
        snprintf(percent, sizeof percent, finished ? "Done" : "%d%%", (int)(fraction * 100));
        ui_text(FONT_SMALL, SCREEN_W - MARGIN - 28 - ui_text_width(FONT_SMALL, percent), y + 2,
                finished ? COLOUR_CLANK : COLOUR_HOLO, percent);
        ui_bar(MARGIN + 28, y + 32, CONTENT_W - 56, 8, fraction, finished ? COLOUR_CLANK : COLOUR_HOLO);
        y += 60;
    }
    ui_wrap(FONT_SMALL, MARGIN + 28, 520, CONTENT_W - 56, COLOUR_INK_FAINT, session->last_log);
    double overall = session_build_fraction(session);
    ui_bar(MARGIN, 598, CONTENT_W, 14, overall, COLOUR_BOLT);

    bool asking = screens->cancel_asked != 0 && SDL_GetTicks() - screens->cancel_asked < CANCEL_WINDOW;
    const int glyphs[] = {back_glyph()};
    const char *const labels[] = {session->cancel_sent ? "Cancelling..." : asking ? "Press again to cancel" : "Cancel"};
    footer(glyphs, labels, 1);
    if (pressed & PAD_BACK && !session->cancel_sent) {
        if (asking) {
            session_cancel(session);
        } else {
            screens->cancel_asked = SDL_GetTicks();
        }
    }
    if (session->build == BUILD_DONE) {
        screens->screen = SCREEN_DONE;
    }
}

/* ---- Done ---- */

static void done_screen(Screens *screens, Session *session, unsigned pressed)
{
    header(session, 3);
    char heading[96];
    snprintf(heading, sizeof heading, "%s is ready", session->edition_name);
    title(heading, COLOUR_CLANK);
    ui_text(FONT_BODY_BOLD, MARGIN, 180, COLOUR_INK, session->output);
    char hash[128];
    snprintf(hash, sizeof hash, "SHA-256 %s%s", session->sha256,
             session->cover_copied ? "  ·  the first disc's cover was copied beside it" : "");
    ui_text(FONT_SMALL, MARGIN, 214, COLOUR_INK_FAINT, hash);

    /* the game's name in the PS2 games list: the new image's file name */
    const char *file = strrchr(session->output, '/');
    file = file != NULL ? file + 1 : session->output;
    char play[400];
    snprintf(play, sizeof play, "Quit the builder, then start %.*s from your PS2 games in webMAN MOD.",
             (int)(strlen(file) > 4 ? strlen(file) - 4 : strlen(file)), file);
    ui_panel(MARGIN, 262, CONTENT_W, 136, 14, COLOUR_PLATE, COLOUR_EDGE);
    ui_text(FONT_LABEL, MARGIN + 28, 280, COLOUR_HOLO, "PLAY IT");
    int y = 312;
    y += ui_wrap(FONT_BODY, MARGIN + 28, y, CONTENT_W - 56, COLOUR_INK, play) + 10;
    ui_wrap(FONT_SMALL, MARGIN + 28, y, CONTENT_W - 56, COLOUR_INK_DIM,
            "If it isn't listed yet, refresh webMAN MOD's games list.");

    const int glyphs[] = {back_glyph(), accept_glyph()};
    const char *const labels[] = {"Quit", "Back to the start"};
    footer(glyphs, labels, 2);
    if (pressed & PAD_ACCEPT) {
        session->build = BUILD_IDLE;
        screens->screen = SCREEN_EDITION;
    } else if (pressed & PAD_BACK) {
        screens->quit = true;
    }
}

/* ---- A newer builder ---- */

static void update_screen(Screens *screens, Session *session, unsigned pressed)
{
    header(session, 0);
    Update update;
    update_get(&update);
    char heading[96];
    snprintf(heading, sizeof heading, "Editions Builder %s", update.latest);
    title(heading, update.state == UPDATE_DOWNLOADED ? COLOUR_CLANK : COLOUR_INK);
    int y = 184;
    if (update.state == UPDATE_DOWNLOADING) {
        ui_text(FONT_BODY, MARGIN, y, COLOUR_INK_DIM, "Downloading the new builder's package from GitHub.");
        ui_bar(MARGIN, y + 52, CONTENT_W, 10, update.fraction, COLOUR_BOLT);
        char percent[16];
        snprintf(percent, sizeof percent, "%d%%", (int)(update.fraction * 100.0 + 0.5));
        ui_text(FONT_SMALL, SCREEN_W - MARGIN - ui_text_width(FONT_SMALL, percent), y + 72, COLOUR_HOLO, percent);
        const int glyphs[] = {back_glyph()};
        const char *const labels[] = {"Stop"};
        footer(glyphs, labels, 1);
        if (pressed & PAD_BACK) {
            update_cancel();
        }
        return;
    }
    if (update.state == UPDATE_DOWNLOADED) {
        char saved[700];
        snprintf(saved, sizeof saved, "Saved as %s.", update.saved);
        ui_text(FONT_BODY, MARGIN, y, COLOUR_INK, saved);
        ui_panel(MARGIN, 236, CONTENT_W, 150, 14, COLOUR_PLATE, COLOUR_EDGE);
        ui_text(FONT_LABEL, MARGIN + 28, 254, COLOUR_HOLO, "INSTALL IT");
        int line = 286;
        line += ui_wrap(FONT_BODY, MARGIN + 28, line, CONTENT_W - 56, COLOUR_INK,
                        "Quit the builder, then install the package from Package Manager \xE2\x80\xBA Install "
                        "Package Files \xE2\x80\xBA Standard.") + 10;
        ui_wrap(FONT_SMALL, MARGIN + 28, line, CONTENT_W - 56, COLOUR_INK_DIM,
                "It replaces this version. The images you've built stay where they are.");
        const int glyphs[] = {back_glyph(), accept_glyph()};
        const char *const labels[] = {"Back", "Quit"};
        footer(glyphs, labels, 2);
        if (pressed & PAD_ACCEPT) {
            screens->quit = true;
        } else if (pressed & PAD_BACK) {
            screens->screen = SCREEN_EDITION;
        }
        return;
    }
    /* failed, or stopped */
    if (update.state == UPDATE_FAILED) {
        char failed[400];
        snprintf(failed, sizeof failed, "The download failed: %s.", update.failure);
        ui_wrap(FONT_BODY, MARGIN, y, CONTENT_W, COLOUR_HAZARD, failed);
    } else {
        ui_text(FONT_BODY, MARGIN, y, COLOUR_INK_DIM, "The download was stopped.");
    }
    const int glyphs[] = {back_glyph(), accept_glyph()};
    const char *const labels[] = {"Back", update.state == UPDATE_FAILED ? "Try again" : "Download"};
    footer(glyphs, labels, 2);
    if (pressed & PAD_ACCEPT) {
        update_download();
    } else if (pressed & PAD_BACK) {
        screens->screen = SCREEN_EDITION;
    }
}

static void problem_screen(Screens *screens, Session *session, unsigned pressed)
{
    header(session, 0);
    title("The builder can't start", COLOUR_HAZARD);
    ui_wrap(FONT_BODY, MARGIN, 184, CONTENT_W, COLOUR_INK, session->problem);
    const int glyphs[] = {back_glyph()};
    const char *const labels[] = {"Quit"};
    footer(glyphs, labels, 1);
    if (pressed & PAD_BACK) {
        screens->quit = true;
    }
}

void screens_frame(Screens *screens, Session *session, unsigned pressed)
{
    screens->frame++;
    ui_background(session->build == BUILD_RUNNING ? 1.0 : 0.0);
    if (session->problem[0] != 0) {
        problem_screen(screens, session, pressed);
        return;
    }
    switch (screens->screen) {
    case SCREEN_EDITION:
        edition_screen(screens, session, pressed);
        break;
    case SCREEN_DISCS:
        discs_screen(screens, session, pressed);
        break;
    case SCREEN_REVIEW:
        review_screen(screens, session, pressed);
        break;
    case SCREEN_BUILD:
        build_screen(screens, session, pressed);
        break;
    case SCREEN_DONE:
        done_screen(screens, session, pressed);
        break;
    case SCREEN_UPDATE:
        update_screen(screens, session, pressed);
        break;
    }
}

const char *screens_state(const Screens *screens, const Session *session)
{
    if (session->problem[0] != 0) {
        return "problem";
    }
    switch (screens->screen) {
    case SCREEN_EDITION:
        return session->have_edition ? "edition" : "starting";
    case SCREEN_DISCS:
        return session_discs_settled(session) ? "discs:ready" : "discs:checking";
    case SCREEN_REVIEW:
        return session->planned ? "review:planned" : "review:planning";
    case SCREEN_BUILD:
        return session->build == BUILD_RUNNING   ? "build:running"
               : session->build == BUILD_FAILED  ? "build:failed"
               : session->build == BUILD_CANCELLED ? "build:cancelled"
                                                   : "build:done";
    case SCREEN_DONE:
        return "build:done";
    case SCREEN_UPDATE: {
        Update update;
        update_get(&update);
        return update.state == UPDATE_DOWNLOADING   ? "update:downloading"
               : update.state == UPDATE_DOWNLOADED ? "update:downloaded"
               : update.state == UPDATE_FAILED     ? "update:failed"
                                                   : "update:stopped";
    }
    }
    return "";
}
