#ifndef HELLO_H
#define HELLO_H

struct canopus_module_descriptor_v1;

/* Returns the hello module descriptor. The generated constructor glue on
 * the device registers this descriptor via the stock .init_array. */
const struct canopus_module_descriptor_v1 *hello_descriptor(void);

#endif /* HELLO_H */
