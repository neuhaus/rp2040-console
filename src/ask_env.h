#ifndef RP2040_CONSOLE_ASK_ENV_H
#define RP2040_CONSOLE_ASK_ENV_H

/* Longest answer accepted; Rogue requires names shorter than 30. */
#define ASK_ENV_MAX 29

/*
 * With ASK_ENV in the manifest, prompt on the console for that environment
 * variable and set it; a no-op otherwise. Called once the environment is in
 * its starting state, before the program runs. See ask_env.c.
 */
void console_ask_env(void);

#endif /* RP2040_CONSOLE_ASK_ENV_H */
