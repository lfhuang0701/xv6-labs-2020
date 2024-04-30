# 1.Trap代码执行流程，以shell执行write系统调用为例

总体框图：

![](https://mit-public-courses-cn-translatio.gitbook.io/~gitbook/image?url=https%3A%2F%2F1977542228-files.gitbook.io%2F%7E%2Ffiles%2Fv0%2Fb%2Fgitbook-legacy-files%2Fo%2Fassets%252F-MHZoT2b_bcLghjAOPsJ%252F-MKFsfImgYCtnwA1d2hO%252F-MKHxleUqYy-y0mrS48w%252Fimage.png%3Falt%3Dmedia%26token%3Dab7c66bc-cf61-4af4-90fd-1fefc96c7b5f&width=768&dpr=4&quality=100&sign=1d837279a6b66bc2b9b89b6bf74ba461029b4c0fc2594301f33a697f6f299061)

系统调用全流程详解：

[[6.S081——陷阱部分(一文读懂xv6系统调用)——xv6源码完全解析系列(5)\_sd ra, 40(a0)-CSDN博客](https://blog.csdn.net/zzy980511/article/details/130255251?ops_request_misc=%257B%2522request%255Fid%2522%253A%2522171444503816800211534199%2522%252C%2522scm%2522%253A%252220140713.130102334.pc%255Fall.%2522%257D&request_id=171444503816800211534199&biz_id=0&utm_medium=distribute.pc_search_result.none-task-blog-2~all~first_rank_ecpm_v1~rank_v31_ecpm-5-130255251-null-null.142^v100^pc_search_result_base8&utm_term=xv86%E7%B3%BB%E7%BB%9F%E8%B0%83%E7%94%A8%E5%85%A8%E6%B5%81%E7%A8%8B&spm=1018.2226.3001.4187)]
