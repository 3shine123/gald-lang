#ifndef NOPA_HELLO_NF_H
#define NOPA_HELLO_NF_H

#include <nopa/object.h>

struct nopa_NFObject_vtable;
struct nopa_Student_vtable;

struct NFObject;
NFObject * NFObject_init(NFObject * self, SEL _cmd);
void NFObject_dealloc(NFObject * self, SEL _cmd);
struct nopa_NFObject_vtable;
struct Student;
int Student_grade(NFObject * self, SEL _cmd);
void Student_setGrade_(NFObject * self, SEL _cmd, int value);
struct nopa_Student_vtable;
extern NFClass nopa_NFObject_class;
extern NFClass nopa_Student_class;
void nopa_init(void);

#endif /* NOPA_HELLO_NF_H */
