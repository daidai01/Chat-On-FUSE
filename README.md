# Chat-On-FUSE

DIY homework: a chat bot based on FUSE

run:

```
$ gcc -Wall daidai.c rbtree/rbtree.c `pkg-config fuse3 --cflags --libs` -o daidai
$ mkdir chat
$ ./daidai chat
$ mkdir chat/bot1
$ mkdir chat/bot2
$ echo "hello" > chat/bot1/bot2
$ cat chat/bot2/bot1
hello
```

