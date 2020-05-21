/*--------------------------------------------------------------------
 
    潜水艦ゲーム	（ミニゲーム）
    
	目的：ターゲットボードにゲームを移植する練習。（以前PCで作成した
        　シューティングゲーム（飛行機もの）を潜水艦ゲームとして作り直す）

    仕様：・タッチパネルでプレイヤーを操作する
        　・スイッチボタンで爆弾を操作する
        　・プロセス間通信を用いる（練習のため）

    ターゲットボード：Armadillo460（embedded system社が作ったデバイス）
                    (ポリテクセンター所有）
    組み込みLinux(Debian)を実装、クロスコンパイル環境で開発

    ・タッチパネルで移動					△
    ・スイッチで爆弾発射----スイッチ反応		 	○
                        ￤_チャタリング対応		○
                        ￤_子プロセスの終了		○
    ・スコア表示						--
    ・HP表示（色で表現）					○
    ・敵が爆弾を発射する				 	--
    ・スタート画面の設定----タッチしたらスタート	        --
                        ￤_難易度選定			--
                        ￤_GAMEOVER 処理		○
                        ￤_GAMECLEAR 処理		○

                            2020/03/17
---------------------------------------------------------------------*/
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/select.h>
#include <tslib.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/msg.h>
#include <sys/ipc.h>
#include <signal.h> //子プロセスを終わらせる用
#include <sys/types.h>
#include "bitmap.h"

//***********
//マクロ定義
//***********
#define SW "/dev/input/event0"      //ボタンスイッチ
#define T_PANEL "/dev/input/event1" //タッチパネル
#define FIFO "testfifo"             // FIFO
#define MMAP "/dev/fb0"
#define SCREENWIDTH 480
#define SCREENHEIGHT 272
#define SCREENSIZE ( SCREENWIDTH * SCREENHEIGHT * 2 )

//色表現
#define RGB( r, g, b ) ( ( ( r ) << 11 ) | ( ( g ) << 5 ) | ( b ) )
#include "draw.h" //ターゲットボードに描画用

#define SW1 158
#define SW2 139

#define Enemy_N 10 //敵の数
#define Bomb_N 50 //爆弾の数(画面外or敵に当たればリロードされる）
#define Enemy_HP 10  //敵の体力
#define Player_HP 30 // playerの体力

//---------------
//グローバル変数
//---------------

typedef struct { //すべての元となる構造体
    int x;
    int y;
    int w;
    int h;
    int vx;
    int vy;
    int HP;
} OBJ;

//タッチパネル用
struct ts_sample samp;

//スクリーン用のフレームバッファ。_b,_cはチラつき防止用
unsigned short *pfb, *pfb_b, *pfb_c;

//画面背景、ゲームオーバー背景、クリア背景(480*272)
char *bmpfile[] = {"sea.bmp", "gameover.bmp", "clear.bmp", NULL};

//----------------
//プロトタイプ宣言
//----------------
//ベースとなる処理
OBJ initOBJ( int x, int y, int w, int h, int vx, int vy, int HP );
OBJ move( OBJ obj );

//プレイヤーの処理
OBJ  touchMove( OBJ player );
void drawPlayer( OBJ player );
void deathPlayer( OBJ player );

void initBomb( OBJ bomb[], OBJ player );
void drawBomb( OBJ bomb[] );
void moveBomb( OBJ bomb[], OBJ player );
void initEnemy( OBJ enemy[] );
void drawEnemy( OBJ enemy[] );
void moveEnemy( OBJ enemy[] );
int  hit( OBJ bomb, OBJ enemy );
void check_Player_Enemy( OBJ player, OBJ enemy[] );
void check_Bomb_Enemy( OBJ bomb[], OBJ enemy[] );
int  clearCheck( OBJ enemy[] );
void endGame( OBJ enemy[] );

//デバッグ用。iには数字を入れる
void E( int i );

//****************
//エントリーポイント
//****************
int main( void ) {

    //全体のループ
    while( 1 ) {

        //------------
        //初期設定
        //------------

        OBJ player;
        OBJ enemy[ Enemy_N ];
        OBJ bomb[ Bomb_N ];

        int                i, j;
        int                swfd; // SW用
        struct input_event ev;   //インプットイベント構造体
        int                mmfd; //画面表示用

        //タッチパネル用
        int           ret;
        struct tsdev *ts;

        //子プロセスのkill用
        pid_t swpid;

        //メッセージキューのキー
        int   msgid;
        key_t msgkey;
        struct {
            long mtype;
            char mtext[ 256 ];
        } buf;

        // msgsendはこのプロセスのプログラムファイル名
        if( ( msgkey = ftok( "submarine", 'a' ) ) == -1 ) { // ID取得・生成
            //エラー表示
            perror( "ftok" );
            return 1;
        }

        //メッセージキューの生成・取得
        if( ( msgid = msgget( msgkey, 0666 | IPC_CREAT ) ) == -1 ) {
            //エラー表示
            perror( "msgget" );
            return 1;
        }

#if 1
        //-------------------------
        //ｓｗ処理
        //子プロセス（爆弾＿SW制御）
        //-------------------------
        if( ( swpid = fork() ) == 0 ) {

            // msgsendはこのプロセスのプログラムファイル名
            if( ( msgkey = ftok( "submarine", 'a' ) ) == -1 ) { // ID取得・生成
                //エラー表示
                perror( "ftok" );
                return 1;
            }

            //メッセージキューの生成・取得
            if( ( msgid = msgget( msgkey, 0666 | IPC_CREAT ) ) == -1 ) {
                //エラー表示
                perror( "msgget" );
                return 1;
            }

            // SWデバイスのファイルを開く（読み込み）
            swfd = open( SW, O_RDONLY );

            while( 1 ) {
                read( swfd, &ev, sizeof( ev ) );
                switch( ev.type ) {
                    //イベント終了
                    case EV_SYN:
                        printf( "EV_SYN\n" );
                        break;
                    //イベント発生
                    case EV_KEY:
                        printf( "EV_KEY code = %d value = %d\n", ev.code,
                                ev.value );
                        // SWが押された時のみ反応
                        if( ev.value == 1 ) {
                            if( ev.code == SW1 ) {
                                //場合分けしたい時に有効
                                buf.mtext[ 0 ] = ev.code;
                                buf.mtype      = 1; //タイプは必ず設定
                                //メッセージの送信
                                if( msgsnd( msgid, &buf, sizeof( buf ), 0 ) ==
                                    -1 ) {
                                    //エラー表示
                                    perror( "msgsnd" );
                                    E( 0 ); //デバッグ０
                                    return 1;
                                }
                                usleep( 15000 ); //チャタリングカット
                                E( 1 );          //デバッグ１
                            }
                        }
                        break;
                }
            }
            close( swfd );
            _exit( 1 );
        }
#endif

        // mmapデバイスのファイルを開く
        mmfd = open( MMAP, O_RDWR );

        // mmapによりバッファの先頭アドレスを取得
        pfb =
            mmap( 0, SCREENSIZE, PROT_READ | PROT_WRITE, MAP_SHARED, mmfd, 0 );
        ts  = ts_open( T_PANEL, 1 );
        ret = ts_config( ts );

        //仮想フレームバッファ、メモリ確保
        pfb_b = malloc( SCREENSIZE );
        pfb_c = malloc( SCREENSIZE );

        //画面を初期化
        load_bmp( bmpfile[ 0 ], pfb_c, SCREENWIDTH, SCREENHEIGHT ); //海中画像

        // Playerの初期化
        player = initOBJ( 100, 150, 40, 12, 0, 0, Player_HP );

        //爆弾の初期化
        initBomb( bomb, player );

        // Enemyの初期化
        initEnemy( enemy );

        //**************************
        //メインループ（ゲームプレイ）
        //**************************
        while( 1 ) {
            //タッチパネルから情報を読み取り
            ret = ts_read( ts, &samp, 1 );

            //画面を初期化
            memcpy( pfb_b, pfb_c, SCREENSIZE ); //背景をpbf_bに渡す

            // Enemy描画
            drawEnemy( enemy );
            // Player描画
            drawPlayer( player );
            // bomb描画
            drawBomb( bomb );

            // bombとenemyの当たり判定
            check_Bomb_Enemy( bomb, enemy );

            // playerとEnemyの当たり判定
            check_Player_Enemy( player, enemy );

            //敵が全滅ならばクリア！
            if( clearCheck( enemy ) ) {
                load_bmp( bmpfile[ 2 ], pfb, SCREENWIDTH,
                          SCREENHEIGHT ); //クリア画像
                //	memcpy(pfb,pfb_c,SCREENSIZE);	//描画
                sleep( 5 );
                break;
            }
            //ゲームオーバー！
            if( player.HP <= 0 ) {
                deathPlayer( player );
                sleep( 5 ); //ゲーム終了してから、５秒後に再開
                break;      //初期設定画面に戻る
            }

            //仮想バッファからコピーして描画
            memcpy( pfb, pfb_b, SCREENSIZE );

            // player移動
            player = touchMove( player );
            // enemy移動
            moveEnemy( enemy );

            // swが押された時
            if( msgrcv( msgid, &buf, sizeof( buf ), 0, IPC_NOWAIT ) != -1 ) {
                printf( "rcv\n" ); // swが押される（メッセージ受信したとき）
                //ポーリング中
                //	if(ev.code == SW1){
                for( i = 0; i < Bomb_N; i++ ) {
                    if( bomb[ i ].HP == 1 )
                        continue;
                    else if( bomb[ i ].HP == 0 ) {
                        bomb[ i ].HP = 1;        //描画タイプにセット
                        bomb[ i ].x  = player.x; //爆弾発射地点を代入
                        bomb[ i ].vx = 5;
                        E( bomb[ i ].x );
                        break;
                    }
                }
            }
            // Bombの移動処理
            moveBomb( bomb, player );
            //タッチ情報表示
            printf( "x:%6d y:%6d p:%6d\n", samp.x, samp.y, samp.pressure );
            //コマを停止させる
            usleep( 33000 );
        }
        ts_close( ts );
        close( mmfd );
        kill( swpid, SIGTERM );
    }
    return 0;
}

//------------//
//	関数定義  //
//------------//
//すべてのOBJの初期化関数
OBJ initOBJ( int x, int y, int w, int h, int vx, int vy, int HP ) {
    OBJ obj;
    obj.x  = x;
    obj.y  = y;
    obj.w  = w;
    obj.h  = h;
    obj.vx = vx;
    obj.vy = vy;
    obj.HP = HP;
    return obj;
}

//すべての動きの基本
OBJ move( OBJ obj ) {
    obj.x += obj.vx;
    obj.y += obj.vy;
    return obj;
};

//キャラの移動関数
OBJ touchMove( OBJ player ) {
    if( samp.pressure == 1 ) { //タッチパネルを触ったら

        if( samp.x < 100 ) //画面左
            player.vx = -3;
        else if( samp.x > SCREENWIDTH - 80 ) //画面右
            player.vx = 3;
        if( samp.y < 80 ) //画面上
            player.vy = -3;
        else if( samp.y > SCREENHEIGHT - 80 ) //画面下
            player.vy = 3;
    } else { //触らないときは移動量０に
        player.vx = 0;
        player.vy = 0;
    }
    //移動量を代入
    player = move( player );

    //範囲指定(画面から出ないように）
    if( player.x <= 40 ) {
        player.x  = 40;
        player.vx = 0;
    } else if( player.x >= 440 ) {
        player.x  = 440;
        player.vx = 0;
    }
    if( player.y <= 20 ) {
        player.y  = 20;
        player.vy = 0;
    } else if( player.y >= 255 ) {
        player.y  = 255;
        player.vy = 0;
    }
    return player;
}

// Playerキャラの描画
void drawPlayer( OBJ player ) {       //潜水艦の形
    intval = ( Player_HP - player.HP ); //体力を、潜水艦の色で表現
    intlife;
    if( player.HP <= 0 ) val = 15;
    lcd_drw_rec( pfb_b, player.x - 14, player.y - 18, player.x + 14, player.y,
                 RGB( 13 + val, 15 + val, 7 + val ), 1 );
    lcd_drw_ell( pfb_b, player.x, player.y, player.w, player.h,
                 RGB( 16 + val, 18 + val, 8 + val ), 1 );
}

// Playerのゲームオーバー処理
void deathPlayer( OBJ player ) {
    drawPlayer( player );
    sleep( 1 );
    load_bmp( bmpfile[ 1 ], pfb_c, SCREENWIDTH,
              SCREENHEIGHT );         //ゲームオーバー画像
    memcpy( pfb, pfb_c, SCREENSIZE ); //描画
}

// Bombの初期化
void initBomb( OBJ bomb[], OBJ player ) {
    int i;
    for( i = 0; i < Bomb_N; i++ ) // playerの位置からスタート
        bomb[ i ] = initOBJ( player.x + 2, player.y + 5, 12, 4, 0, 0,
                             0 ); //最初は描画しないHP==0で初期化
}

// bombの移動処理
void moveBomb( OBJ bomb[], OBJ player ) { // 2020/03/16
    inti;
    for( i = 0; i < Bomb_N; i++ ) { //魚雷を直進させる

        //爆弾を描画しないとき
        if( bomb[ i ].HP == 0 ) {
            // playerの現在地にリセット
            bomb[ i ].x = player.x;
            bomb[ i ].y = player.y;
            continue;
        }
        //爆弾が画面を超えた時
        else if( bomb[ i ].x >= 470 && bomb[ i ].HP == 1 ) {
            // playerの現在地にリセット
            bomb[ i ].x  = player.x;
            bomb[ i ].y  = player.y;
            bomb[ i ].HP = 0; //描画しない
            continue;
        }
        //それ以外（HP==1)の時
        bomb[ i ] = move( bomb[ i ] ); //動かす
    }
}

// Bombの描画
void drawBomb( OBJ bomb[] ) {
    int i;
    for( i = 0; i < Bomb_N; i++ ) {
        if( bomb[ i ].HP == 0 ) continue;
        lcd_drw_ell( pfb_b, bomb[ i ].x, bomb[ i ].y, bomb[ i ].w, bomb[ i ].h,
                     RGB( 5, 10, 5 ), 1 );
    }
}

// Enemyの初期化
void initEnemy( OBJ enemy[] ) {
    int i;
    for( i = 0; i < Enemy_N; i++ )
        enemy[ i ] =
            initOBJ( rand() % 200 + 250, rand() % 250 + 60, rand() % 50 + 20,
                     10, rand() % 4 + 1, rand() % 3 - 1, Enemy_HP );
}

// Enemyキャラの描画
void drawEnemy( OBJ enemy[] ) {
    int i;
    int cnt = 1;
    for( i = 0; i < Enemy_N; i++ ) {
        if( enemy[ i ].HP <= 0 ) continue; //死んでる時は描画しない
        lcd_drw_rec( pfb_b, enemy[ i ].x - 13, enemy[ i ].y - 18,
                     enemy[ i ].x + 13, enemy[ i ].y,
                     RGB( 10, 15, 15 - 2 * enemy[ i ].HP ), 1 );
        lcd_drw_ell( pfb_b, enemy[ i ].x, enemy[ i ].y, enemy[ i ].w,
                     enemy[ i ].h, RGB( 10, 14, 15 - 2 * enemy[ i ].HP ), 1 );
    }
}

// Enemyキャラの移動処理
void moveEnemy( OBJ enemy[] ) {
    int i;
    for( i = 0; i < Enemy_N; i++ ) { //ゆらゆら表現(上下に動いて避ける）
        enemy[ i ].vx = -1 * ( rand() % 7 + 1 );
        enemy[ i ].vy = rand() % 10 - 4;
        enemy[ i ]    = move( enemy[ i ] );

        if( enemy[ i ].x <= 25 ) {
            enemy[ i ].x = 470;
            enemy[ i ].y = rand() % 240 + 20;
        }
        if( enemy[ i ].y <= 25 ) enemy[ i ].y = 25;
    }
}

//ぶつかったか否かの判定
int hit( OBJ bomb, OBJ enemy ) {
    if( abs( bomb.x - enemy.x ) <= 12 && abs( bomb.y - enemy.y ) <= 15 )
        return 1;
    else
        return 0;
}

//敵とぶつかった時の処理
void check_Player_Enemy( OBJ player, OBJ enemy[] ) {
    int i;
    for( i = 0; i < Enemy_N; i++ ) {
        if( hit( player, enemy[ i ] ) == 1 &&
            enemy[ i ].HP >= 1 ) { // hitしたとき
            player.HP -= 2;        // playerHPを２減らす
            enemy[ i ].HP--;       //敵HPを１減らす
        }
    }
}

//爆弾があたったか否かの判定まとめ
void check_Bomb_Enemy( OBJ bomb[], OBJ enemy[] ) {
    int i, j;
    for( i = 0; i < Bomb_N; i++ ) {
        for( j = 0; j < Enemy_N; j++ ) {
            if( hit( bomb[ i ], enemy[ j ] ) == 1 &
                enemy[ j ].HP >= 1 ) { // hitしたとき
                if( bomb[ i ].HP == 1 ) {
                    bomb[ i ].HP = 0; //ボムを描画しない
                    enemy[ j ].HP--;  // HPを減らす
                }
            }
        }
    }
}

//クリア条件を満たしているかチェックする
int clearCheck( OBJ enemy[] ) {
    int i;
    for( i = 0; i < Enemy_N; i++ ) {
        if( enemy[ i ].HP >= 1 ) return 0;
    }
    return 1;
}

//デバッグ用関数
void E( int i ) { printf( "error%d!\n", i ); }
