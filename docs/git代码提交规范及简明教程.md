# git 代码提交简明规范

### 一、分支名规范

对于个人开发版本：

分支名格式：

> 你的姓名/特性关键字/分支内容名

（分支内容名第一个字母应该小写，多个单词使用驼峰式）

**新功能或者特性分支名格式：**

```
yourName/feat/branchName

例如
zhangxf/feat/parseSqlPattern
```

**修复bug**

```
yourName/fix/branchName

例如
zhangxf/fix/fixSelectServiceTime
```



**特性关键字有如下类型值**：

```
//既有feat，又有fix或者其他使用feat
feat: 功能或者特性修改
fix: 修复bug
chore：非功能性修改，例如修正代码风格、修正错别字、重命名变量等
docs:文档性更新
test：增加测试。
merge：代码合并。
```









### 二、提交记录规范

特性关键字:（一个空格）提交记录（多个提交内容以分号分割）

```
例如：
feat: add http msg interfaces
fix: fix timerid
chore: fix typos
merge: merge from httplib
test: add test case for httplib
feat: support http; fix something
```

**提交规范注意事项：**

1. 提交的commit记录要以功能点为单位，最终的commit记录不要出现许多中间态的提交记录

   ```
   假设A同学现在新增一个sql解析功能，第一次提交发现存在一个bug，进行了第二次提交，测试后发现仍然没完全修复，做了第三次提交，提三次提交完成后，经测试功能完全正常，那么在合入主干分支，应该将这三条记录压缩成一条提交记录。
   再假设B同学对原来的某个项目去除编译警告，第一次去了一部分，第二次又去掉一部分，那么第二次应该使用修订提交，将所有去除编译警告的修改作为一个提交。
   ```

2. 拉取代码或者合并分支时，要使用`git rebase`或者`--rebase`参数消除无意义的提交。

   ```
   假设master分支上有A、B、C三个提交记录，现在基于master分支创建一个新的分支叫zhangxf/feat/fileMgr分支，新增了三个commit：D、E、F，master分支在这期间又新增了G、H、I三个commit，那么在开发zhangxf/feat/fileMgr分支期间，拉取最新代码使用 git pull 应该带上 --rebase 选项，将该分支变基到master的提交I的位置处。
   ```

3. `git merge`或者将自己的分支合并到主分支，应该对分支进行挤压（squash），以功能点为单位，再干净地合入主分支。

   ```
   假设自己的开发分支zhangxf/feat/fileMgr一共有6个提交：A1、A2、A3、B1、B2、B3，前三个与功能A有关，后三个与功能B有关，那么在合入主分支之前，应该将它们挤压成A、B两个提交。演示如下：
   $ git log --oneline
   e7e31c7 (HEAD -> zhangxf/feat/fileMgr, origin/zhangxf/feat/fileMgr) feat: B3
   80550e4 feat: B2
   a4f054c feat: B1
   5dfcfdf feat: A3
   b81a5fb feat: A2
   e204199 feat: A1
   89703de feat: 添加主程序测试代码
   a356def Initial commit
   
   $ git rebase -i 89703de #89703de是挤压记录之前的一条记录
   
   pick e204199 feat: A1  #pick表示保留该记录
   s b81a5fb feat: A2     #s即squash，表示挤压到前一条记录中
   s 5dfcfdf feat: A3     #s即squash，表示挤压到前一条记录中
   pick a4f054c feat: B1  #pick表示保留该记录
   s 80550e4 feat: B2     #s即squash，表示挤压到前一条记录中
   s e7e31c7 feat: B3     #s即squash，表示挤压到前一条记录中
   
   # Rebase df066cc..e7e31c7 onto df066cc (6 commands)
   #
   # Commands:
   # p, pick <commit> = use commit
   # r, reword <commit> = use commit, but edit the commit message
   # e, edit <commit> = use commit, but stop for amending
   # s, squash <commit> = use commit, but meld into previous commit
   # f, fixup [-C | -c] <commit> = like "squash" but keep only the previous
   #                    commit's log message, unless -C is used, in which case
   #                    keep only this commit's message; -c is same as -C but
   #                    opens the editor
   # x, exec <command> = run command (the rest of the line) using shell
   # b, break = stop here (continue rebase later with 'git rebase --continue')
   # d, drop <commit> = remove commit
   # l, label <label> = label current HEAD with a name
   
   #再次修改commit消息，将A1、A2和A3的commit消息合成一条
   # This is a combination of 3 commits.
   # This is the 1st commit message:
   
   feat: A
   
   # This is the commit message #2:
   
   #feat: A2
   
   # This is the commit message #3:
   
   #feat: A3
   
   # Please enter the commit message for your changes. Lines starting
   # with '#' will be ignored, and an empty message aborts the commit.
   #
   # Date:      Mon Oct 21 14:27:08 2024 +0800
   #
   # interactive rebase in progress; onto df066cc
   # Last commands done (3 commands done):
   
   #再次修改commit消息，将B1、B2和B3的commit消息合成一条
   # This is a combination of 3 commits.
   # This is the 1st commit message:
   
   feat: B
   
   # This is the commit message #2:
   
   #feat: B2
   
   # This is the commit message #3:
   
   #feat: B3
   
   # Please enter the commit message for your changes. Lines starting
   # with '#' will be ignored, and an empty message aborts the commit.
   #
   # Date:      Mon Oct 21 14:29:16 2024 +0800
   #
   # interactive rebase in progress; onto df066cc
   
   $ git log --oneline
   8a3835a (HEAD -> zhangxf/feat/fileMgr) feat: B
   454ce01 feat: A
   ```

   



### 三、常用 git 速成命令

```
#查看提交记录
git log
#查看简明的提交记录
git log --oneline

#在本地仓库创建名为zhangxf/feat/youAreHandsome的分支
git checkout -b zhangxf/feat/youAreHandsome

#将本地分支zhangxf/feat/youAreHandsome推送到远端，即在远端仓库产生一个同名的分支
git push --set-upstream origin zhangxf/feat/youAreHandsome

#在本地仓库创建名为zhangxf/feat/youAreHandsome的分支，并与远程仓库名为zhangxf/feat/helloWorld的分支对应，
#远程分支zhangxf/feat/helloWorld必须存在
#注意，通常本地分支名应该和远程分支名保持一致
git checkout -b zhangxf/feat/youAreHandsome origin/zhangxf/feat/helloWorld
#在本地仓库创建名为zhangxf/feat/youAreHandsome的分支，并与远程仓库名为zhangxf/feat/youAreHandsome的分支的分支对应，
#远程分支zhangxf/feat/youAreHandsome的分支必须存在
git checkout -b zhangxf/feat/youAreHandsome origin/zhangxf/feat/youAreHandsome

#在本地仓库创建名为zhangxf/feat/youAreHandsome的分支，并与远程仓库名为myTag的tag对应
git checkout -b zhangxf/feat/youAreHandsome myTag

#在本地删除名为zhangxf/feat/youAreHandsome的分支，并同步删除远端对应的分支
git branch -D zhangxf/feat/youAreHandsome
git push

#添加someFile.go至stage区域，为提交做准备
git add someFile.go
#添加当前所有改动的文件至stage区域（会自动忽略加入到.gitignore中的文件），为提交做准备(-A的就是All的意思)
git add -A
#将本地所有添加到staged区域的文件提交到本地仓库，并指定提交消息，提交消息规范参照上文
git commit -m "feat: 新增了sql解析功能"
#将本地仓库内容推送到远端仓库，如果远端仓库当前分支有本地不存在的新内容，则命令无法执行成功
git push
#将本地仓库内容强行推送到远端仓库，如果远端仓库当前分支有本地不存在的新内容，则会覆盖远端仓库内容，可能会造成其他人的提交丢失
#谨慎使用，公共分支不允许使用-f选项（--force）
git push -f

#用本次的提交来替换上一次的提交记录，执行该命令之后会弹出一个修改上一次提交记录msg的对话框，此时可对上一次的commit msg进行修改
#这个操作叫作“修正提交”
#注意：修正提交的代码提交时间会显示原提交记录时间，而不是更新后的提交时间
git commit --amend
#如果只想替换上一次的提交记录，但不想对commit msg进行修改，则可以使用如下命令
git commit --amend --no-edit

例：假设某次提交修改main.go，但是测试发现没有修改对，后面又做了一次修改，此时就可以使用提交，把对代码review无意义的第一次提交替换掉
git add main.go
git commit -m "fix: fix some bug"
#使用上一次的commit msg，即"fix: fix some bug"
git commit --amend --no-edit
#修正提交使用强推到远端仓库，所以此时要确定要修订的提交之间是否有其他记录，如果有，则不建议使用修订提交，否则会把其他记录给覆盖掉
git push -f

#丢弃本地仓库、staged区域和本地区域的改动
git reset --hard commitID
#丢弃本地仓库、staged区域的改动（--mixed选项是默认值）
git reset --mixed commitID
#仅丢弃本地仓库的改动
git reset --soft commitID

下面的示例演示了git commit --soft的用法
$ git status
On branch zhangxf/feat/fileMgr
Your branch and 'origin/zhangxf/feat/fileMgr' have diverged,
and have 2 and 6 different commits each, respectively.
  (use "git pull" to merge the remote branch into yours)

Untracked files:
  (use "git add <file>..." to include in what will be committed)
        D.txt
        E.txt

nothing added to commit but untracked files present (use "git add" to track)

zhangxf@mydevmachine MINGW64 /d/mygocodes/mainmoduletest (zhangxf/feat/fileMgr)
$ git add D.txt

zhangxf@mydevmachine MINGW64 /d/mygocodes/mainmoduletest (zhangxf/feat/fileMgr)
$ git commit -m "feat: add D.txt"
[zhangxf/feat/fileMgr cfc498c] feat: add D.txt
 1 file changed, 0 insertions(+), 0 deletions(-)
 create mode 100644 D.txt

zhangxf@mydevmachine MINGW64 /d/mygocodes/mainmoduletest (zhangxf/feat/fileMgr)
$ git log --oneline
cfc498c (HEAD -> zhangxf/feat/fileMgr) feat: add D.txt
8a3835a feat: B
454ce01 feat: A
df066cc (origin/master, origin/HEAD, master) feat: A1
89703de feat: 添加主程序测试代码
a356def Initial commit

zhangxf@mydevmachine MINGW64 /d/mygocodes/mainmoduletest (zhangxf/feat/fileMgr)
$ git reset --soft 8a3835a

zhangxf@mydevmachine MINGW64 /d/mygocodes/mainmoduletest (zhangxf/feat/fileMgr)
$ git log --oneline
8a3835a (HEAD -> zhangxf/feat/fileMgr) feat: B
454ce01 feat: A
df066cc (origin/master, origin/HEAD, master) feat: A1
89703de feat: 添加主程序测试代码
a356def Initial commit

zhangxf@mydevmachine MINGW64 /d/mygocodes/mainmoduletest (zhangxf/feat/fileMgr)
$ git status
On branch zhangxf/feat/fileMgr
Your branch and 'origin/zhangxf/feat/fileMgr' have diverged,
and have 2 and 6 different commits each, respectively.
  (use "git pull" to merge the remote branch into yours)

Changes to be committed:
  (use "git restore --staged <file>..." to unstage)
        new file:   D.txt

Untracked files:
  (use "git add <file>..." to include in what will be committed)
        E.txt


zhangxf@mydevmachine MINGW64 /d/mygocodes/mainmoduletest (zhangxf/feat/fileMgr)
$ git add E.txt

zhangxf@mydevmachine MINGW64 /d/mygocodes/mainmoduletest (zhangxf/feat/fileMgr)
$ git commit -m "feat: add D and E"
[zhangxf/feat/fileMgr 7ab7e43] feat: add D and E
 2 files changed, 0 insertions(+), 0 deletions(-)
 create mode 100644 D.txt
 create mode 100644 E.txt

zhangxf@mydevmachine MINGW64 /d/mygocodes/mainmoduletest (zhangxf/feat/fileMgr)
$ git log --oneline
7ab7e43 (HEAD -> zhangxf/feat/fileMgr) feat: add D and E
8a3835a feat: B
454ce01 feat: A
df066cc (origin/master, origin/HEAD, master) feat: A1
89703de feat: 添加主程序测试代码
a356def Initial commit

下面的示例演示了git commit --mixed的用法
zhangxf@mydevmachine MINGW64 /d/mygocodes/mainmoduletest (zhangxf/feat/fileMgr)
$ git reset 8a3835a

zhangxf@mydevmachine MINGW64 /d/mygocodes/mainmoduletest (zhangxf/feat/fileMgr)
$ git log --oneline
8a3835a (HEAD -> zhangxf/feat/fileMgr) feat: B
454ce01 feat: A
df066cc (origin/master, origin/HEAD, master) feat: A1
89703de feat: 添加主程序测试代码
a356def Initial commit

zhangxf@mydevmachine MINGW64 /d/mygocodes/mainmoduletest (zhangxf/feat/fileMgr)
$ git status
On branch zhangxf/feat/fileMgr
Your branch and 'origin/zhangxf/feat/fileMgr' have diverged,
and have 2 and 6 different commits each, respectively.
  (use "git pull" to merge the remote branch into yours)

Untracked files:
  (use "git add <file>..." to include in what will be committed)
        D.txt
        E.txt

nothing added to commit but untracked files present (use "git add" to track)

下面的示例演示了git commit --hard的用法
zhangxf@mydevmachine MINGW64 /d/mygocodes/mainmoduletest (zhangxf/feat/fileMgr)
$ git reset --hard 8a3835a
HEAD is now at 8a3835a feat: B

zhangxf@mydevmachine MINGW64 /d/mygocodes/mainmoduletest (zhangxf/feat/fileMgr)
$ git log --oneline
8a3835a (HEAD -> zhangxf/feat/fileMgr) feat: B
454ce01 feat: A
df066cc (origin/master, origin/HEAD, master) feat: A1
89703de feat: 添加主程序测试代码
a356def Initial commit

zhangxf@mydevmachine MINGW64 /d/mygocodes/mainmoduletest (zhangxf/feat/fileMgr)
$ git status
On branch zhangxf/feat/fileMgr
Your branch and 'origin/zhangxf/feat/fileMgr' have diverged,
and have 2 and 6 different commits each, respectively.
  (use "git pull" to merge the remote branch into yours)

nothing to commit, working tree clean

#git reset和reflog功能可以实现历史操作的快进与后退
zhangxf@mydevmachine MINGW64 /d/mygocodes/mainmoduletest (zhangxf/feat/fileMgr)
$ git reflog
8a3835a (HEAD -> zhangxf/feat/fileMgr) HEAD@{0}: reset: moving to 8a3835a
7ab7e43 HEAD@{1}: reset: moving to 7ab7e43
8a3835a (HEAD -> zhangxf/feat/fileMgr) HEAD@{2}: reset: moving to 8a3835a
7ab7e43 HEAD@{3}: commit: feat: add D and E
8a3835a (HEAD -> zhangxf/feat/fileMgr) HEAD@{4}: reset: moving to 8a3835a
cfc498c HEAD@{5}: commit: feat: add D.txt
8a3835a (HEAD -> zhangxf/feat/fileMgr) HEAD@{6}: rebase (finish): returning to refs/heads/zhangxf/feat/sqlPars
er
8a3835a (HEAD -> zhangxf/feat/fileMgr) HEAD@{7}: rebase (squash): feat: B
a178fc4 HEAD@{8}: rebase (squash): # This is a combination of 2 commits.
a009ad6 HEAD@{9}: rebase (pick): feat: B1
454ce01 HEAD@{10}: rebase (squash): feat: A
4a4e630 HEAD@{11}: rebase (squash): # This is a combination of 2 commits.
e204199 HEAD@{12}: rebase (start): checkout df066cc
e7e31c7 (origin/zhangxf/feat/fileMgr) HEAD@{13}: rebase (finish): returning to refs/heads/zhangxf/feat/sqlPars
er
e7e31c7 (origin/zhangxf/feat/fileMgr) HEAD@{14}: rebase (start): checkout e204199
e7e31c7 (origin/zhangxf/feat/fileMgr) HEAD@{15}: commit: feat: B3
80550e4 HEAD@{16}: commit: feat: B2
a4f054c HEAD@{17}: commit: feat: B1
5dfcfdf HEAD@{18}: commit: feat: A3
b81a5fb HEAD@{19}: commit: feat: A2
e204199 HEAD@{20}: commit: feat: A1

zhangxf@mydevmachine MINGW64 /d/mygocodes/mainmoduletest (zhangxf/feat/fileMgr)
$ git reset 7ab7e43
Unstaged changes after reset:
D       D.txt
D       E.txt

zhangxf@mydevmachine MINGW64 /d/mygocodes/mainmoduletest (zhangxf/feat/fileMgr)
$ git log --oneline
7ab7e43 (HEAD -> zhangxf/feat/fileMgr) feat: add D and E
8a3835a feat: B
454ce01 feat: A
df066cc (origin/master, origin/HEAD, master) feat: A1
89703de feat: 添加主程序测试代码
a356def Initial commit


#暂存当前的分支的改动记录
git stash
#弹出上一次保存的改动记录
git stash pop
#查看暂存列表
git stash list
#应用某一次的暂存记录到当前分支
git stash apply stashID
#丢弃stash list中指定记录
git stash drop stashID

#分支合并
git merge someBranchName

#选择某些commit记录到指定分支“cherry-pick”
git cherry-pick commitID1 commitID2 commitID3
```



