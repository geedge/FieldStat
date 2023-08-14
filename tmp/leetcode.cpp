// 1289. 下降路径最小和 II


// 给你一个 n x n 整数矩阵 grid ，请你返回 非零偏移下降路径 数字和的最小值。

// 非零偏移下降路径 定义为：从 grid 数组中的每一行选择一个数字，且按顺序选出来的数字中，相邻数字不在原数组的同一列。

// https://leetcode.cn/problems/minimum-falling-path-sum-ii/description/

// 百分之一百动态规划
/* 
注意到：一行选哪个不会影响下面第二行选哪个。对于每个位置，记录下来最小的和第二小的就行
选最小的，如果上方的最小和下方的最小都不在同一行。或者选第二小的。
额，总之分支成两种情况。

s[i, j] = s[i - 1, p] + v[i, j] or 
        = s[i - 1, j] + v[i, p]

算是想出来了，但是灵感是copilot给的
*/



#include <vector>
#include <iostream>

using namespace std;

class Solution {
public:
    
};