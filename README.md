Requirements:

- GCC version >= 11  
- enable C++20 support
- to use the latest release of SystemC is highly recommended

---

配置SystemC可以参考我的[博文](https://zhuanlan.zhihu.com/p/638360098)（也参考了很多别人的经验，但这篇比较适合本工程）。

运行`make -j $(nproc)`编译程序。运行`make run`进行仿真测试。如果运行报错，可以先尝试`make clean`.

出现Segmentation fault的调试方法：  
参考[Linux下Segmentation Fault的定位方法](https://blog.csdn.net/whahu1989/article/details/110881842)、[linux下不产生core文件的原因](https://blog.csdn.net/qq_35621436/article/details/120870746)。  
调试步骤（调试完记得把core文件删除，文件太大上传到github会报错）：

```bash
make gdb
```
 
根据行号设置断点：
```text
(gdb) b 5
```
运行和继续
```text
运行 r
继续单步调试 n
继续执行到下一个断点 c
```

---

To configure SystemC, you can refer to my [blog post](https://zhuanlan.zhihu.com/p/638360098).

Run `make -j $(nproc)` to compile the program. Run `make run` to start simulation. If you encounters an error, you can try `make clean` command first.

---

Acknowledgement: [ventus-gpgpu](https://github.com/THU-DSP-LAB/ventus-gpgpu), [GPGPU-Sim](https://github.com/accel-sim/gpgpu-sim_distribution)

---

# TODO

endprg时，通过置wait_bran为1来停止该warp的dispatch。在UPDATE_SCORE中notify ev_issue来触发ev_issue_list，逻辑复杂了点。这都不太好。