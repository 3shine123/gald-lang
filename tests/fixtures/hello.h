#ifndef NOPA_HELLO_NP_H
#define NOPA_HELLO_NP_H

#include <nopa/object.h>

struct nopa_NPObject_vtable;
struct nopa_Student_vtable;

struct NPObject;
NPObject * NPObject_init(NPObject * self, SEL _cmd);
void NPObject_dealloc(NPObject * self, SEL _cmd);
struct nopa_NPObject_vtable;
struct Student;
int Student_grade(NPObject * self, SEL _cmd);
void Student_setGrade_(NPObject * self, SEL _cmd, int value);
struct nopa_Student_vtable;
extern NPClass nopa_NPObject_class;
extern NPClass nopa_Student_class;
void nopa_init(void);

#endif /* NOPA_HELLO_NP_H */
