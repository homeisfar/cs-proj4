#ifndef USERPROG_PAGEDIR_H
#define USERPROG_PAGEDIR_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

uint32_t *pagedir_create (void);
void pagedir_destroy (uint32_t *pd);
bool pagedir_set_page (uint32_t *pd, void *upage, void *kpage, bool rw);
void *pagedir_get_page (uint32_t *pd, const void *upage);
void pagedir_clear_page (uint32_t *pd, void *upage);
bool pagedir_is_dirty (uint32_t *pd, const void *upage);
void pagedir_set_dirty (uint32_t *pd, const void *upage, bool dirty);
bool pagedir_is_accessed (uint32_t *pd, const void *upage);
void pagedir_set_accessed (uint32_t *pd, const void *upage, bool accessed);
void pagedir_activate (uint32_t *pd);

void *push (void *esp, uintptr_t data, size_t size);
void *pop (void *esp, void *pass_back_ptr, size_t size);
void peek (void *esp, void *pass_back_ptr, size_t size);

#endif /* userprog/pagedir.h */
