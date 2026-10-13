#ifndef RP2040_CONSOLE_ENV_H
#define RP2040_CONSOLE_ENV_H

/*
 * A process environment, because a terminal game expects one.
 *
 * newlib has getenv() but nothing populates an environment here, so it
 * answers NULL for every name - and TERM is not a nicety. Anything driving
 * a terminal through termcap asks for it before it has drawn anything, and
 * treats its absence as a reason to give up.
 *
 * So the engine keeps a small table. Its contents come from the game's
 * manifest, which is the point: what a particular program needs in its
 * environment is the program's business, not the engine's.
 */

/* Fill the table from the entries the game declared. Call before main(). */
void env_init(void);
void env_reset(void);

#endif /* RP2040_CONSOLE_ENV_H */
