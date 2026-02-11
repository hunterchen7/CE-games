#include <graphx.h>
#include <keypadc.h>
#include <sys/util.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

/* Screen dimensions */
#define SCREEN_W 320
#define SCREEN_H 240

/* Paddle dimensions (width and margin are fixed; height varies per level) */
#define PADDLE_W 4
#define PADDLE_MARGIN 8
#define AI_PADDLE_H 32  /* AI paddle stays fixed; player paddle shrinks */

/* Ball */
#define BALL_SIZE 4

/* Framerate */
#define TARGET_FPS 30
#define FRAME_TIME (CLOCKS_PER_SEC / TARGET_FPS)

/* Palette indices */
#define PAL_BG        0
#define PAL_PADDLE1   1
#define PAL_PADDLE2   2
#define PAL_BALL      3
#define PAL_NET       4
#define PAL_TEXT      5
#define PAL_HIGHLIGHT 6
#define PAL_HEART     7

/* Game constants */
#define NUM_LEVELS 5
#define TRANSITION_FRAMES 90
#define START_LIVES 3
#define INFINITE_MAX_SCORE 30 /* difficulty maxes out at this score */

/* ---------- types ---------- */

typedef struct {
    uint16_t bg;
    uint16_t paddle1;
    uint16_t paddle2;
    uint16_t ball;
    uint16_t net;
    uint16_t text;
    uint16_t highlight;
} color_theme_t;

typedef struct {
    int ball_speed;
    int ball_dy_max;
    int paddle_h;
    int player_speed;
    int ai_speed;
    int points_to_win;
    color_theme_t theme;
} level_config_t;

typedef enum {
    STATE_MENU,
    STATE_LEVEL_SELECT,
    STATE_PLAYING,
    STATE_LEVEL_COMPLETE,
    STATE_GAME_OVER
} game_state_t;

/* ---------- level data ---------- */

static const char *level_names[NUM_LEVELS] = {
    "Classic",
    "Warm Up",
    "Getting Serious",
    "Fast Lane",
    "Intense"
};


static const level_config_t levels[NUM_LEVELS] = {
    /* Level 1 — easy, slow AI */
    { 4, 2, 40, 6, 2, 5,
      { gfx_RGBTo1555(0,0,40),    gfx_RGBTo1555(80,180,255),
        gfx_RGBTo1555(255,100,100), gfx_RGBTo1555(255,255,0),
        gfx_RGBTo1555(60,60,100),  gfx_RGBTo1555(255,255,255),
        gfx_RGBTo1555(255,255,0) } },
    /* Level 2 */
    { 4, 4, 36, 6, 3, 5,
      { gfx_RGBTo1555(0,30,0),    gfx_RGBTo1555(0,255,100),
        gfx_RGBTo1555(255,160,0),  gfx_RGBTo1555(255,255,255),
        gfx_RGBTo1555(0,60,0),     gfx_RGBTo1555(200,255,200),
        gfx_RGBTo1555(0,255,100) } },
    /* Level 3 */
    { 6, 4, 32, 6, 4, 5,
      { gfx_RGBTo1555(30,0,40),   gfx_RGBTo1555(255,100,200),
        gfx_RGBTo1555(0,220,220),  gfx_RGBTo1555(255,200,50),
        gfx_RGBTo1555(60,0,80),    gfx_RGBTo1555(220,180,255),
        gfx_RGBTo1555(255,100,200) } },
    /* Level 4 */
    { 6, 6, 28, 8, 5, 5,
      { gfx_RGBTo1555(40,0,0),    gfx_RGBTo1555(255,215,0),
        gfx_RGBTo1555(192,192,192), gfx_RGBTo1555(255,60,60),
        gfx_RGBTo1555(80,0,0),     gfx_RGBTo1555(255,200,200),
        gfx_RGBTo1555(255,215,0) } },
    /* Level 5 — hardest */
    { 8, 6, 24, 8, 6, 5,
      { gfx_RGBTo1555(5,5,15),    gfx_RGBTo1555(0,255,0),
        gfx_RGBTo1555(255,0,255),  gfx_RGBTo1555(255,255,255),
        gfx_RGBTo1555(30,30,50),   gfx_RGBTo1555(0,255,255),
        gfx_RGBTo1555(0,255,0) } },
};

/* ---------- global state ---------- */

static game_state_t state;
static int current_level;
static int menu_cursor;
static int level_select_cursor;
static int running;

/* gameplay */
static int paddle1_y, paddle2_y;
static int ball_x, ball_y;
static int ball_dx, ball_dy;
static int score1, score2;
static int transition_timer;
static int lives;
static int infinite_mode;
static int game_over_win; /* 1 = won campaign, 0 = lost (out of lives) */
static int paused;
static int last_scorer; /* 1 = AI (left), 2 = player (right) */
static int ball_speed_fp; /* fixed-point speed (x256) for exponential growth */

/* active config — mutable copy used by all gameplay functions */
static level_config_t active_cfg;

/* keyboard snapshot (read once per frame after kb_Scan) */
static uint8_t cur_g1, cur_g6, cur_g7;
static uint8_t prev_g1, prev_g6, prev_g7;

/* ---------- helpers ---------- */

static void apply_theme(const color_theme_t *t)
{
    gfx_palette[PAL_BG]        = t->bg;
    gfx_palette[PAL_PADDLE1]   = t->paddle1;
    gfx_palette[PAL_PADDLE2]   = t->paddle2;
    gfx_palette[PAL_BALL]      = t->ball;
    gfx_palette[PAL_NET]       = t->net;
    gfx_palette[PAL_TEXT]      = t->text;
    gfx_palette[PAL_HIGHLIGHT] = t->highlight;
    gfx_palette[PAL_HEART]     = gfx_RGBTo1555(255, 0, 40);
}

/* Linearly interpolate: start + (end - start) * n / d */
static int lerp(int start, int end, int n, int d)
{
    return start + (end - start) * n / d;
}

/* Generate a random bright color (at least one channel > 128) */
static uint16_t rand_bright_color(void)
{
    uint8_t r = rand() & 255;
    uint8_t g = rand() & 255;
    uint8_t b = rand() & 255;
    /* boost a random channel to ensure brightness */
    switch (rand() % 3)
    {
        case 0: r |= 0xC0; break;
        case 1: g |= 0xC0; break;
        case 2: b |= 0xC0; break;
    }
    return gfx_RGBTo1555(r, g, b);
}

static void compute_infinite_cfg(void)
{
    int s = score2;
    if (s > INFINITE_MAX_SCORE) s = INFINITE_MAX_SCORE;

    active_cfg.ball_speed   = lerp(4, 8, s, INFINITE_MAX_SCORE);
    active_cfg.ball_dy_max  = lerp(2, 8, s, INFINITE_MAX_SCORE);
    active_cfg.paddle_h     = lerp(40, 20, s, INFINITE_MAX_SCORE);
    active_cfg.player_speed = lerp(6, 10, s, INFINITE_MAX_SCORE);
    active_cfg.ai_speed     = lerp(2, 6, s, INFINITE_MAX_SCORE);
    active_cfg.points_to_win = 9999;

    /* random color theme every point */
    active_cfg.theme.bg        = gfx_RGBTo1555(rand() % 30, rand() % 30, rand() % 30);
    active_cfg.theme.paddle1   = rand_bright_color();
    active_cfg.theme.paddle2   = rand_bright_color();
    active_cfg.theme.ball      = rand_bright_color();
    active_cfg.theme.net       = gfx_RGBTo1555(40 + rand() % 40, 40 + rand() % 40, 40 + rand() % 40);
    active_cfg.theme.text      = gfx_RGBTo1555(200 + rand() % 56, 200 + rand() % 56, 200 + rand() % 56);
    active_cfg.theme.highlight = rand_bright_color();
    apply_theme(&active_cfg.theme);
}

static void reset_ball(void)
{
    ball_speed_fp = (active_cfg.ball_speed + 2) << 8;
    ball_dy = (rand() % active_cfg.ball_dy_max) + 1;
    if (rand() & 1) ball_dy = -ball_dy;

    if (last_scorer == 1)
    {
        /* AI scored — AI serves from left paddle */
        ball_x = PADDLE_MARGIN + PADDLE_W;
        ball_y = paddle1_y + AI_PADDLE_H / 2 - BALL_SIZE / 2;
        ball_dx = active_cfg.ball_speed + 2;
    }
    else
    {
        /* player scored — player serves from right paddle */
        ball_x = SCREEN_W - PADDLE_MARGIN - PADDLE_W - BALL_SIZE;
        ball_y = paddle2_y + active_cfg.paddle_h / 2 - BALL_SIZE / 2;
        ball_dx = -(active_cfg.ball_speed + 2);
    }
}

static void start_level(void)
{
    memcpy(&active_cfg, &levels[current_level], sizeof(level_config_t));
    apply_theme(&active_cfg.theme);
    paddle1_y = SCREEN_H / 2 - AI_PADDLE_H / 2;
    paddle2_y = SCREEN_H / 2 - active_cfg.paddle_h / 2;
    score1 = 0;
    score2 = 0;
    lives = START_LIVES;
    infinite_mode = 0;
    paused = 0;
    last_scorer = 1; /* AI serves first */
    reset_ball();
}

static void start_infinite(void)
{
    infinite_mode = 1;
    current_level = 0;
    score1 = 0;
    score2 = 0;
    lives = START_LIVES;
    paused = 0;
    last_scorer = 1; /* AI serves first */
    compute_infinite_cfg();
    paddle1_y = SCREEN_H / 2 - AI_PADDLE_H / 2;
    paddle2_y = SCREEN_H / 2 - active_cfg.paddle_h / 2;
    reset_ball();
}

/* ---------- gameplay ---------- */

static void draw_net(void)
{
    int y;
    gfx_SetColor(PAL_NET);
    for (y = 0; y < SCREEN_H; y += 8)
    {
        gfx_FillRectangle_NoClip(SCREEN_W / 2 - 1, y, 2, 4);
    }
}

/*
 * 7x6 pixel heart bitmap (1 = filled, 0 = transparent)
 *  .XX.XX.
 *  XXXXXXX
 *  XXXXXXX
 *  .XXXXX.
 *  ..XXX..
 *  ...X...
 */
static const uint8_t heart_bmp[6] = {
    0x6C, /* 0110 1100 */
    0xFE, /* 1111 1110 */
    0xFE, /* 1111 1110 */
    0x7C, /* 0111 1100 */
    0x38, /* 0011 1000 */
    0x10, /* 0001 0000 */
};

static void draw_heart(int x, int y)
{
    int row, col;
    gfx_SetColor(PAL_HEART);
    for (row = 0; row < 6; row++)
    {
        for (col = 0; col < 7; col++)
        {
            if (heart_bmp[row] & (0x80 >> col))
                gfx_SetPixel(x + col, y + row);
        }
    }
}

static void draw_lives(void)
{
    int i;
    for (i = 0; i < lives; i++)
    {
        draw_heart(SCREEN_W - 12 - i * 10, 3);
    }
}

static void update_ai(void)
{
    int target_y, delta, spd;

    if (ball_dx < 0 && ball_x < SCREEN_W / 2)
    {
        /* ball heading toward AI and past midline — track it */
        target_y = ball_y - AI_PADDLE_H / 2;
        spd = active_cfg.ai_speed;
    }
    else
    {
        /* ball heading away or on far side — drift to center */
        target_y = SCREEN_H / 2 - AI_PADDLE_H / 2;
        spd = (active_cfg.ai_speed + 1) / 2;
    }

    delta = target_y - paddle1_y;

    if (delta > spd)
        paddle1_y += spd;
    else if (delta < -spd)
        paddle1_y -= spd;
    else
        paddle1_y = target_y;

    if (paddle1_y < 0) paddle1_y = 0;
    if (paddle1_y > SCREEN_H - AI_PADDLE_H) paddle1_y = SCREEN_H - AI_PADDLE_H;
}

static void update_input(void)
{
    if (cur_g7 & kb_Up)   paddle2_y -= active_cfg.player_speed;
    if (cur_g7 & kb_Down) paddle2_y += active_cfg.player_speed;

    if (paddle2_y < 0) paddle2_y = 0;
    if (paddle2_y > SCREEN_H - active_cfg.paddle_h) paddle2_y = SCREEN_H - active_cfg.paddle_h;
}

static void update_ball(void)
{
    ball_x += ball_dx;
    ball_y += ball_dy;

    /* top/bottom bounce */
    if (ball_y <= 0) { ball_y = 0; ball_dy = -ball_dy; }
    if (ball_y >= SCREEN_H - BALL_SIZE) { ball_y = SCREEN_H - BALL_SIZE; ball_dy = -ball_dy; }

    /* left paddle (AI) collision */
    if (ball_x <= PADDLE_MARGIN + PADDLE_W &&
        ball_y + BALL_SIZE >= paddle1_y &&
        ball_y <= paddle1_y + AI_PADDLE_H &&
        ball_dx < 0)
    {
        ball_x = PADDLE_MARGIN + PADDLE_W;
        ball_speed_fp = ball_speed_fp * 105 / 100;
        ball_dx = ball_speed_fp >> 8;
    }

    /* right paddle (player) collision */
    if (ball_x + BALL_SIZE >= SCREEN_W - PADDLE_MARGIN - PADDLE_W &&
        ball_y + BALL_SIZE >= paddle2_y &&
        ball_y <= paddle2_y + active_cfg.paddle_h &&
        ball_dx > 0)
    {
        ball_x = SCREEN_W - PADDLE_MARGIN - PADDLE_W - BALL_SIZE;
        ball_speed_fp = ball_speed_fp * 105 / 100;
        ball_dx = -(ball_speed_fp >> 8);
    }

    /* player scores (ball passed AI) */
    if (ball_x < 0)
    {
        score2++;
        last_scorer = 2;
        if (infinite_mode)
            compute_infinite_cfg();
        reset_ball();
    }

    /* AI scores (ball passed player) — lose a life */
    if (ball_x > SCREEN_W)
    {
        score1++;
        last_scorer = 1;
        lives--;
        if (lives <= 0)
        {
            game_over_win = 0;
            state = STATE_GAME_OVER;
            transition_timer = TRANSITION_FRAMES;
            return;
        }
        reset_ball();
    }

    /* campaign: level advance when player reaches threshold */
    if (!infinite_mode && score2 >= active_cfg.points_to_win)
    {
        if (current_level < NUM_LEVELS - 1)
        {
            state = STATE_LEVEL_COMPLETE;
            transition_timer = TRANSITION_FRAMES;
        }
        else
        {
            game_over_win = 1;
            state = STATE_GAME_OVER;
            transition_timer = TRANSITION_FRAMES;
        }
    }
}

static void draw_game(void)
{
    gfx_FillScreen(PAL_BG);
    draw_net();

    /* paddles */
    gfx_SetColor(PAL_PADDLE1);
    gfx_FillRectangle_NoClip(PADDLE_MARGIN, paddle1_y, PADDLE_W, AI_PADDLE_H);
    gfx_SetColor(PAL_PADDLE2);
    gfx_FillRectangle_NoClip(SCREEN_W - PADDLE_MARGIN - PADDLE_W, paddle2_y, PADDLE_W, active_cfg.paddle_h);

    /* ball */
    gfx_SetColor(PAL_BALL);
    gfx_FillRectangle_NoClip(ball_x, ball_y, BALL_SIZE, BALL_SIZE);

    /* scores (AI left, Player right) */
    gfx_SetTextScale(2, 2);
    gfx_SetTextFGColor(PAL_TEXT);
    gfx_SetTextXY(SCREEN_W / 2 - 40, 5);
    gfx_PrintInt(score1, 1);
    gfx_SetTextXY(SCREEN_W / 2 + 25, 5);
    gfx_PrintInt(score2, 1);

    /* level / mode indicator */
    gfx_SetTextScale(1, 1);
    gfx_SetTextFGColor(PAL_NET);
    gfx_SetTextXY(2, 2);
    if (infinite_mode)
        gfx_PrintString("INF");
    else
    {
        gfx_PrintString("Lv");
        gfx_PrintInt(current_level + 1, 1);
    }

    /* lives */
    draw_lives();

    gfx_SwapDraw();
}

static void update_playing(void)
{
    uint8_t new6 = cur_g6 & ~prev_g6;
    uint8_t new1 = cur_g1 & ~prev_g1;

    if (cur_g6 & kb_Clear)
    {
        state = STATE_MENU;
        apply_theme(&levels[0].theme);
        return;
    }

    /* toggle pause */
    if ((new6 & kb_Enter) || (new1 & kb_2nd))
        paused = !paused;

    if (paused)
    {
        draw_game();
        /* draw pause overlay on the back buffer, then swap again */
        gfx_SetTextScale(2, 2);
        gfx_SetTextFGColor(PAL_TEXT);
        gfx_PrintStringXY("PAUSED", 104, 110);
        gfx_SetTextScale(1, 1);
        gfx_SetTextFGColor(PAL_NET);
        gfx_PrintStringXY("enter to resume", 100, 140);
        gfx_SwapDraw();
        return;
    }

    update_ai();
    update_input();
    update_ball();

    if (state == STATE_PLAYING)
        draw_game();
}

/* ---------- menu ---------- */

#define MENU_ITEMS 3

static void draw_menu_item(int idx, int y, const char *text, int text_x)
{
    int text_w = (int)strlen(text) * 16; /* 2x scale = 16px per char */
    int bar_x = text_x - 6;
    int bar_w = text_w + 12;

    if (menu_cursor == idx)
    {
        gfx_SetColor(PAL_HIGHLIGHT);
        gfx_FillRectangle_NoClip(bar_x, y - 2, bar_w, 22);
        gfx_SetTextFGColor(PAL_BG);
    }
    else
    {
        gfx_SetTextFGColor(PAL_TEXT);
    }
    gfx_PrintStringXY(text, text_x, y);
}

static void draw_menu(void)
{
    gfx_FillScreen(PAL_BG);

    /* title */
    gfx_SetTextScale(3, 3);
    gfx_SetTextFGColor(PAL_BALL);
    gfx_PrintStringXY("PONG", 112, 35);

    /* decorative line */
    gfx_SetColor(PAL_NET);
    gfx_HorizLine_NoClip(80, 67, 160);

    /* menu items */
    gfx_SetTextScale(2, 2);
    draw_menu_item(0, 85, "play", 132);
    draw_menu_item(1, 117, "infinite", 112);
    draw_menu_item(2, 149, "levels", 120);

    /* help text */
    gfx_SetTextScale(1, 1);
    gfx_SetTextFGColor(PAL_NET);
    gfx_PrintStringXY("arrows: move  enter: select  clear: quit", 12, 222);

    gfx_SwapDraw();
}

static void update_menu(void)
{
    uint8_t new7 = cur_g7 & ~prev_g7;
    uint8_t new6 = cur_g6 & ~prev_g6;
    uint8_t new1 = cur_g1 & ~prev_g1;

    if ((new7 & kb_Down) && menu_cursor < MENU_ITEMS - 1) menu_cursor++;
    if ((new7 & kb_Up) && menu_cursor > 0)                menu_cursor--;

    if ((new6 & kb_Enter) || (new1 & kb_2nd))
    {
        if (menu_cursor == 0)
        {
            current_level = 0;
            start_level();
            state = STATE_PLAYING;
        }
        else if (menu_cursor == 1)
        {
            start_infinite();
            state = STATE_PLAYING;
        }
        else
        {
            level_select_cursor = 0;
            state = STATE_LEVEL_SELECT;
        }
        return;
    }

    if (new6 & kb_Clear)
    {
        running = 0;
        return;
    }

    draw_menu();
}

/* ---------- level select ---------- */

static void draw_level_select(void)
{
    int i, y;

    gfx_FillScreen(PAL_BG);

    gfx_SetTextScale(2, 2);
    gfx_SetTextFGColor(PAL_TEXT);
    gfx_PrintStringXY("Level Select", 68, 15);

    gfx_SetTextScale(1, 1);
    for (i = 0; i < NUM_LEVELS; i++)
    {
        y = 50 + i * 25;

        if (i == level_select_cursor)
        {
            gfx_SetColor(PAL_HIGHLIGHT);
            gfx_FillRectangle_NoClip(40, y - 2, 240, 18);
            gfx_SetTextFGColor(PAL_BG);
        }
        else
        {
            gfx_SetTextFGColor(PAL_TEXT);
        }

        gfx_SetTextXY(50, y);
        gfx_PrintString("Level ");
        gfx_PrintInt(i + 1, 1);
        gfx_PrintString(" - ");
        gfx_PrintString(level_names[i]);
    }

    gfx_SetTextFGColor(PAL_NET);
    gfx_PrintStringXY("arrows: move  enter: select  clear: quit", 12, 222);

    gfx_SwapDraw();
}

static void update_level_select(void)
{
    uint8_t new7 = cur_g7 & ~prev_g7;
    uint8_t new6 = cur_g6 & ~prev_g6;
    uint8_t new1 = cur_g1 & ~prev_g1;

    if ((new7 & kb_Down) && level_select_cursor < NUM_LEVELS - 1) level_select_cursor++;
    if ((new7 & kb_Up) && level_select_cursor > 0)                level_select_cursor--;

    /* preview hovered theme */
    apply_theme(&levels[level_select_cursor].theme);

    if ((new6 & kb_Enter) || (new1 & kb_2nd))
    {
        current_level = level_select_cursor;
        start_level();
        state = STATE_PLAYING;
        return;
    }

    if (cur_g6 & kb_Clear)
    {
        state = STATE_MENU;
        apply_theme(&levels[0].theme);
        return;
    }

    draw_level_select();
}

/* ---------- transition screens ---------- */

static void draw_level_complete(void)
{
    gfx_FillScreen(PAL_BG);

    gfx_SetTextScale(2, 2);
    gfx_SetTextFGColor(PAL_HIGHLIGHT);
    gfx_SetTextXY(44, 70);
    gfx_PrintString("Level ");
    gfx_PrintInt(current_level + 1, 1);
    gfx_PrintString(" Complete!");

    gfx_SetTextScale(1, 1);
    gfx_SetTextFGColor(PAL_TEXT);
    gfx_SetTextXY(108, 110);
    gfx_PrintString("Score: ");
    gfx_PrintInt(score1, 1);
    gfx_PrintString(" - ");
    gfx_PrintInt(score2, 1);

    gfx_SetTextFGColor(PAL_NET);
    gfx_PrintStringXY("Press Enter to continue", 72, 150);

    gfx_SwapDraw();
}

static void update_level_complete(void)
{
    uint8_t new6 = cur_g6 & ~prev_g6;
    uint8_t new1 = cur_g1 & ~prev_g1;

    if ((new6 & kb_Enter) || (new1 & kb_2nd))
    {
        current_level++;
        start_level();
        state = STATE_PLAYING;
        return;
    }

    draw_level_complete();
}

static void draw_game_over(void)
{
    gfx_FillScreen(PAL_BG);

    gfx_SetTextScale(3, 3);
    gfx_SetTextFGColor(PAL_HIGHLIGHT);

    if (game_over_win)
    {
        gfx_PrintStringXY("YOU WIN!", 64, 50);

        gfx_SetTextScale(2, 2);
        gfx_SetTextFGColor(PAL_TEXT);
        gfx_PrintStringXY("All levels", 80, 100);
        gfx_PrintStringXY("complete!", 88, 130);
    }
    else
    {
        gfx_PrintStringXY("GAME OVER", 40, 50);

        gfx_SetTextScale(2, 2);
        gfx_SetTextFGColor(PAL_TEXT);

        if (infinite_mode)
        {
            gfx_SetTextXY(80, 100);
            gfx_PrintString("Score: ");
            gfx_PrintInt(score2, 1);
        }
        else
        {
            gfx_SetTextXY(72, 100);
            gfx_PrintString("Level ");
            gfx_PrintInt(current_level + 1, 1);

            gfx_SetTextXY(80, 130);
            gfx_PrintString("Score: ");
            gfx_PrintInt(score1, 1);
            gfx_PrintString(" - ");
            gfx_PrintInt(score2, 1);
        }
    }

    gfx_SetTextScale(1, 1);
    gfx_SetTextFGColor(PAL_NET);
    gfx_PrintStringXY("Press Enter for menu", 72, 200);

    gfx_SwapDraw();
}

static void update_game_over(void)
{
    transition_timer--;

    if (transition_timer <= 0 || (cur_g6 & kb_Enter) || (cur_g1 & kb_2nd))
    {
        state = STATE_MENU;
        apply_theme(&levels[0].theme);
        return;
    }

    draw_game_over();
}

/* ---------- main ---------- */

int main(void)
{
    clock_t frame_start;

    gfx_Begin();
    gfx_SetDrawBuffer();

    srand(clock());
    state = STATE_MENU;
    menu_cursor = 0;
    running = 1;
    apply_theme(&levels[0].theme);

    do
    {
        frame_start = clock();
        kb_Scan();

        /* snapshot key state once per frame */
        cur_g1 = kb_Data[1];
        cur_g6 = kb_Data[6];
        cur_g7 = kb_Data[7];

        switch (state)
        {
            case STATE_MENU:           update_menu();           break;
            case STATE_LEVEL_SELECT:   update_level_select();   break;
            case STATE_PLAYING:        update_playing();        break;
            case STATE_LEVEL_COMPLETE: update_level_complete();  break;
            case STATE_GAME_OVER:      update_game_over();      break;
        }

        /* save snapshot for next-frame debounce */
        prev_g1 = cur_g1;
        prev_g6 = cur_g6;
        prev_g7 = cur_g7;

        while (clock() - frame_start < FRAME_TIME)
            ;

    } while (running);

    gfx_End();
    return 0;
}
