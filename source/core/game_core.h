#ifndef ANDORSTRAIL_GAME_CORE_H
#define ANDORSTRAIL_GAME_CORE_H

typedef struct {
    unsigned char reserved;
} GameCore;

#ifdef __cplusplus
extern "C" {
#endif

void game_core_init(GameCore *core);
void game_core_update(GameCore *core);
void game_core_shutdown(GameCore *core);

#ifdef __cplusplus
}
#endif

#endif