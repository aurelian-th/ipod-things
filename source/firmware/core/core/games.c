/* SPDX-License-Identifier: GPL-3.0-only */

/* A game that built its own playback list leaves the App's and the Now Playing
 * model's current items on a record of that list. The runtime teardown replaces
 * the list when the game ends and tells neither, so both point into freed memory
 * until the next current-item event; Play in that window reads them and the data
 * abort stops the iPod. After every game end the UI task runs both refreshes,
 * which copy the player's current item or drop theirs when there is none. */

#include "hooks/sites.h"
#include "osos-2.0.5.h"

void core_games_bind(void);

/* Runs on the UI task once the game's runtime has stopped, and may run twice
 * for one game end. */
static uint32_t reset_after_game(void *service, const struct core_next *next)
{
    uint32_t result = core_next_call(next, (uint32_t)service, 0, 0, 0);
    /* Replacing the list leaves the player's index at -1, so App_SyncCurrentItem
     * sees a changed index whenever the App's item came from the old list. */
    if (App)
        App_SyncCurrentItem(App);
    /* NowPlayingModel_Get would create the model; one that does not exist yet
     * holds no item. */
    if (NowPlayingModel_Instance)
        NowPlayingModel_RefreshCurrentItem(NowPlayingModel_Instance);
    return result;
}

static const struct core_hook reset_hook = {CORE_SITE_GamesService_ResetAfterGame_entry,
                                            CORE_HOOK_FILTER, 0, reset_after_game};

void core_games_bind(void)
{
    core_hook_bind(&reset_hook);
}
