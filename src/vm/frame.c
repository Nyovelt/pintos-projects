#include "vm/frame.h"
#include "threads/thread.h"

static struct hash frame_table;
static struct lock lock;

void
frame_table_init()
{

}