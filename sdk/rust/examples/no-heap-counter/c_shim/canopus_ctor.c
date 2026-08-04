/*
 * canopus_ctor.c — constructor/destructor glue for the stock modlib loader
 * (CAN-RUST-004).
 *
 * The stock loader runs .init_array then retains .fini_array for rmmod. The
 * Rust module must not carry a personality/unwind implementation; this C shim
 * provides the minimal constructor that (1) lets the loader discover the
 * module descriptor and (2) runs the module's prepare path before any use.
 * The identity guard itself lives inside the Rust module's activate path and
 * fails closed on a firmware mismatch.
 */

__attribute__((constructor)) static void canopus_mod_ctor(void)
{
    extern int canopus_mod_prepare(const void *ctx);
    extern const void *canopus_module_descriptor_ptr(void);

    (void)canopus_module_descriptor_ptr();
    (void)canopus_mod_prepare(0);
}

__attribute__((destructor)) static void canopus_mod_dtor(void)
{
    extern int canopus_mod_stop(const void *ctx);
    (void)canopus_mod_stop(0);
}
