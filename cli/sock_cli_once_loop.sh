#!/bin/bash
#
function help(){
echo "
# sock_cli_once_loop 单次单固定数据, loop 测试客户端(非dns port 型)
# 目的: 可以在不填满socket 的情况下, 循环测试固定的数据内容, 检索服务器逻辑压力
# 注意: 为了测试使用时的便捷性, 请自行修改关键信息
";
return;
}
#
######################
# 关键信息如下:
test_count="500"
srv_ip="127.0.0.1"
srv_port="6666"
#__sleep="1" # 测试间隔休眠(单位秒)
######################
#
# 不接受参数输入
if [ $# != "0" ];then
  (help)
  exit
fi
#
# 执行测试
# 软件执行语句...
(./sock_cli_once_loop "$test_count" "$srv_ip" "$srv_port" "hello adan_srv-v5...please answer me !!")
#sleep "$__sleep"




