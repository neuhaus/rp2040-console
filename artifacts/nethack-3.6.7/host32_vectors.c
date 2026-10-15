/* Reset vectors for the MPS2 Cortex-M3 semihosted generator runner. */
extern void _mainCRTStartup(void);
__attribute__((used, section(".isr_vector"))) void *host32_vectors[] = {
    (void *) 0x203ff000, (void *) _mainCRTStartup
};
