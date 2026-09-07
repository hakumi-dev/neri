#ifndef NERI_TERMINAL_INTERNAL_H
#define NERI_TERMINAL_INTERNAL_H
#ifdef __cplusplus
extern "C" {
#endif
void neri_terminal_restore(void);
int neri_terminal_active(void);
int neri_interrupt_active(void);
void neri_interrupt_restore(void);
#ifdef __cplusplus
}
#endif
#endif
