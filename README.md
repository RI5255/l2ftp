# l2ftp(Layer 2 File Transmission Protcol)
## About
- seccamp2022「ロバストプロコルチャレンジ」に向けて作成中のプロトコル
- 平均パケット損失率が50%のLANでファイルを速く、正確に送るために設計されている。

## Setup
以下のコマンドを実行してVLANを作成します。sudo権限が必要かもしれません。
```bash
# create network namespaces
$ ip netns add taro
$ ip netns add hanako

# create cables(veth pairs)
$ ip netns exec taro ip link add eth2 type veth peer name eth2 netns hanako

# link up
$ ip netns exec taro ip link set eth2 up
$ ip netns exec hanako ip link set eth2 up
```