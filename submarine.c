/*--------------------------------------------------------------------
 
    �����̓Q�[��	�i�~�j�Q�[���j
    
	�ړI�F�^�[�Q�b�g�{�[�h�ɃQ�[�����ڐA������K�B�i�ȑOPC�ō쐬����
        �@�V���[�e�B���O�Q�[���i��s�@���́j������̓Q�[���Ƃ��č�蒼���j

    �d�l�F�E�^�b�`�p�l���Ńv���C���[�𑀍삷��
        �@�E�X�C�b�`�{�^���Ŕ��e�𑀍삷��
        �@�E�v���Z�X�ԒʐM��p����i���K�̂��߁j

    �^�[�Q�b�g�{�[�h�FArmadillo460�iembedded system�Ђ�������f�o�C�X�j
                    (�|���e�N�Z���^�[���L�j
    �g�ݍ���Linux(Debian)�������A�N���X�R���p�C�����ŊJ��

    �E�^�b�`�p�l���ňړ�					��
    �E�X�C�b�`�Ŕ��e����----�X�C�b�`����		 	��
                        �U_�`���^�����O�Ή�		��
                        �U_�q�v���Z�X�̏I��		��
    �E�X�R�A�\��						--
    �EHP�\���i�F�ŕ\���j					��
    �E�G�����e�𔭎˂���				 	--
    �E�X�^�[�g��ʂ̐ݒ�----�^�b�`������X�^�[�g	        --
                        �U_��Փx�I��			--
                        �U_GAMEOVER ����		��
                        �U_GAMECLEAR ����		��

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
#include <signal.h> //�q�v���Z�X���I��点��p
#include <sys/types.h>
#include "bitmap.h"

//***********
//�}�N����`
//***********
#define SW "/dev/input/event0"      //�{�^���X�C�b�`
#define T_PANEL "/dev/input/event1" //�^�b�`�p�l��
#define FIFO "testfifo"             // FIFO
#define MMAP "/dev/fb0"
#define SCREENWIDTH 480
#define SCREENHEIGHT 272
#define SCREENSIZE ( SCREENWIDTH * SCREENHEIGHT * 2 )

//�F�\��
#define RGB( r, g, b ) ( ( ( r ) << 11 ) | ( ( g ) << 5 ) | ( b ) )
#include "draw.h" //�^�[�Q�b�g�{�[�h�ɕ`��p

#define SW1 158
#define SW2 139

#define Enemy_N 10 //�G�̐�
#define Bomb_N 50 //���e�̐�(��ʊOor�G�ɓ�����΃����[�h�����j
#define Enemy_HP 10  //�G�̗̑�
#define Player_HP 30 // player�̗̑�

//---------------
//�O���[�o���ϐ�
//---------------

typedef struct { //���ׂĂ̌��ƂȂ�\����
    int x;
    int y;
    int w;
    int h;
    int vx;
    int vy;
    int HP;
} OBJ;

//�^�b�`�p�l���p
struct ts_sample samp;

//�X�N���[���p�̃t���[���o�b�t�@�B_b,_c�̓`�����h�~�p
unsigned short *pfb, *pfb_b, *pfb_c;

//��ʔw�i�A�Q�[���I�[�o�[�w�i�A�N���A�w�i(480*272)
char *bmpfile[] = {"sea.bmp", "gameover.bmp", "clear.bmp", NULL};

//----------------
//�v���g�^�C�v�錾
//----------------
//�x�[�X�ƂȂ鏈��
OBJ initOBJ( int x, int y, int w, int h, int vx, int vy, int HP );
OBJ move( OBJ obj );

//�v���C���[�̏���
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

//�f�o�b�O�p�Bi�ɂ͐���������
void E( int i );

//****************
//�G���g���[�|�C���g
//****************
int main( void ) {

    //�S�̂̃��[�v
    while( 1 ) {

        //------------
        //�����ݒ�
        //------------

        OBJ player;
        OBJ enemy[ Enemy_N ];
        OBJ bomb[ Bomb_N ];

        int                i, j;
        int                swfd; // SW�p
        struct input_event ev;   //�C���v�b�g�C�x���g�\����
        int                mmfd; //��ʕ\���p

        //�^�b�`�p�l���p
        int           ret;
        struct tsdev *ts;

        //�q�v���Z�X��kill�p
        pid_t swpid;

        //���b�Z�[�W�L���[�̃L�[
        int   msgid;
        key_t msgkey;
        struct {
            long mtype;
            char mtext[ 256 ];
        } buf;

        // msgsend�͂��̃v���Z�X�̃v���O�����t�@�C����
        if( ( msgkey = ftok( "submarine", 'a' ) ) == -1 ) { // ID�擾�E����
            //�G���[�\��
            perror( "ftok" );
            return 1;
        }

        //���b�Z�[�W�L���[�̐����E�擾
        if( ( msgid = msgget( msgkey, 0666 | IPC_CREAT ) ) == -1 ) {
            //�G���[�\��
            perror( "msgget" );
            return 1;
        }

#if 1
        //-------------------------
        //��������
        //�q�v���Z�X�i���e�QSW����j
        //-------------------------
        if( ( swpid = fork() ) == 0 ) {

            // msgsend�͂��̃v���Z�X�̃v���O�����t�@�C����
            if( ( msgkey = ftok( "submarine", 'a' ) ) == -1 ) { // ID�擾�E����
                //�G���[�\��
                perror( "ftok" );
                return 1;
            }

            //���b�Z�[�W�L���[�̐����E�擾
            if( ( msgid = msgget( msgkey, 0666 | IPC_CREAT ) ) == -1 ) {
                //�G���[�\��
                perror( "msgget" );
                return 1;
            }

            // SW�f�o�C�X�̃t�@�C�����J���i�ǂݍ��݁j
            swfd = open( SW, O_RDONLY );

            while( 1 ) {
                read( swfd, &ev, sizeof( ev ) );
                switch( ev.type ) {
                    //�C�x���g�I��
                    case EV_SYN:
                        printf( "EV_SYN\n" );
                        break;
                    //�C�x���g����
                    case EV_KEY:
                        printf( "EV_KEY code = %d value = %d\n", ev.code,
                                ev.value );
                        // SW�������ꂽ���̂ݔ���
                        if( ev.value == 1 ) {
                            if( ev.code == SW1 ) {
                                //�ꍇ�������������ɗL��
                                buf.mtext[ 0 ] = ev.code;
                                buf.mtype      = 1; //�^�C�v�͕K���ݒ�
                                //���b�Z�[�W�̑��M
                                if( msgsnd( msgid, &buf, sizeof( buf ), 0 ) ==
                                    -1 ) {
                                    //�G���[�\��
                                    perror( "msgsnd" );
                                    E( 0 ); //�f�o�b�O�O
                                    return 1;
                                }
                                usleep( 15000 ); //�`���^�����O�J�b�g
                                E( 1 );          //�f�o�b�O�P
                            }
                        }
                        break;
                }
            }
            close( swfd );
            _exit( 1 );
        }
#endif

        // mmap�f�o�C�X�̃t�@�C�����J��
        mmfd = open( MMAP, O_RDWR );

        // mmap�ɂ��o�b�t�@�̐擪�A�h���X���擾
        pfb =
            mmap( 0, SCREENSIZE, PROT_READ | PROT_WRITE, MAP_SHARED, mmfd, 0 );
        ts  = ts_open( T_PANEL, 1 );
        ret = ts_config( ts );

        //���z�t���[���o�b�t�@�A�������m��
        pfb_b = malloc( SCREENSIZE );
        pfb_c = malloc( SCREENSIZE );

        //��ʂ�������
        load_bmp( bmpfile[ 0 ], pfb_c, SCREENWIDTH, SCREENHEIGHT ); //�C���摜

        // Player�̏�����
        player = initOBJ( 100, 150, 40, 12, 0, 0, Player_HP );

        //���e�̏�����
        initBomb( bomb, player );

        // Enemy�̏�����
        initEnemy( enemy );

        //**************************
        //���C�����[�v�i�Q�[���v���C�j
        //**************************
        while( 1 ) {
            //�^�b�`�p�l���������ǂݎ��
            ret = ts_read( ts, &samp, 1 );

            //��ʂ�������
            memcpy( pfb_b, pfb_c, SCREENSIZE ); //�w�i��pbf_b�ɓn��

            // Enemy�`��
            drawEnemy( enemy );
            // Player�`��
            drawPlayer( player );
            // bomb�`��
            drawBomb( bomb );

            // bomb��enemy�̓����蔻��
            check_Bomb_Enemy( bomb, enemy );

            // player��Enemy�̓����蔻��
            check_Player_Enemy( player, enemy );

            //�G���S�łȂ�΃N���A�I
            if( clearCheck( enemy ) ) {
                load_bmp( bmpfile[ 2 ], pfb, SCREENWIDTH,
                          SCREENHEIGHT ); //�N���A�摜
                //	memcpy(pfb,pfb_c,SCREENSIZE);	//�`��
                sleep( 5 );
                break;
            }
            //�Q�[���I�[�o�[�I
            if( player.HP <= 0 ) {
                deathPlayer( player );
                sleep( 5 ); //�Q�[���I�����Ă���A�T�b��ɍĊJ
                break;      //�����ݒ��ʂɖ߂�
            }

            //���z�o�b�t�@����R�s�[���ĕ`��
            memcpy( pfb, pfb_b, SCREENSIZE );

            // player�ړ�
            player = touchMove( player );
            // enemy�ړ�
            moveEnemy( enemy );

            // sw�������ꂽ��
            if( msgrcv( msgid, &buf, sizeof( buf ), 0, IPC_NOWAIT ) != -1 ) {
                printf( "rcv\n" ); // sw���������i���b�Z�[�W��M�����Ƃ��j
                //�|�[�����O��
                //	if(ev.code == SW1){
                for( i = 0; i < Bomb_N; i++ ) {
                    if( bomb[ i ].HP == 1 )
                        continue;
                    else if( bomb[ i ].HP == 0 ) {
                        bomb[ i ].HP = 1;        //�`��^�C�v�ɃZ�b�g
                        bomb[ i ].x  = player.x; //���e���˒n�_����
                        bomb[ i ].vx = 5;
                        E( bomb[ i ].x );
                        break;
                    }
                }
            }
            // Bomb�̈ړ�����
            moveBomb( bomb, player );
            //�^�b�`���\��
            printf( "x:%6d y:%6d p:%6d\n", samp.x, samp.y, samp.pressure );
            //�R�}���~������
            usleep( 33000 );
        }
        ts_close( ts );
        close( mmfd );
        kill( swpid, SIGTERM );
    }
    return 0;
}

//------------//
//	�֐���`  //
//------------//
//���ׂĂ�OBJ�̏������֐�
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

//���ׂĂ̓����̊�{
OBJ move( OBJ obj ) {
    obj.x += obj.vx;
    obj.y += obj.vy;
    return obj;
};

//�L�����̈ړ��֐�
OBJ touchMove( OBJ player ) {
    if( samp.pressure == 1 ) { //�^�b�`�p�l����G������

        if( samp.x < 100 ) //��ʍ�
            player.vx = -3;
        else if( samp.x > SCREENWIDTH - 80 ) //��ʉE
            player.vx = 3;
        if( samp.y < 80 ) //��ʏ�
            player.vy = -3;
        else if( samp.y > SCREENHEIGHT - 80 ) //��ʉ�
            player.vy = 3;
    } else { //�G��Ȃ��Ƃ��͈ړ��ʂO��
        player.vx = 0;
        player.vy = 0;
    }
    //�ړ��ʂ���
    player = move( player );

    //�͈͎w��(��ʂ���o�Ȃ��悤�Ɂj
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

// Player�L�����̕`��
void drawPlayer( OBJ player ) {       //�����͂̌`
    intval = ( Player_HP - player.HP ); //�̗͂��A�����͂̐F�ŕ\��
    intlife;
    if( player.HP <= 0 ) val = 15;
    lcd_drw_rec( pfb_b, player.x - 14, player.y - 18, player.x + 14, player.y,
                 RGB( 13 + val, 15 + val, 7 + val ), 1 );
    lcd_drw_ell( pfb_b, player.x, player.y, player.w, player.h,
                 RGB( 16 + val, 18 + val, 8 + val ), 1 );
}

// Player�̃Q�[���I�[�o�[����
void deathPlayer( OBJ player ) {
    drawPlayer( player );
    sleep( 1 );
    load_bmp( bmpfile[ 1 ], pfb_c, SCREENWIDTH,
              SCREENHEIGHT );         //�Q�[���I�[�o�[�摜
    memcpy( pfb, pfb_c, SCREENSIZE ); //�`��
}

// Bomb�̏�����
void initBomb( OBJ bomb[], OBJ player ) {
    int i;
    for( i = 0; i < Bomb_N; i++ ) // player�̈ʒu����X�^�[�g
        bomb[ i ] = initOBJ( player.x + 2, player.y + 5, 12, 4, 0, 0,
                             0 ); //�ŏ��͕`�悵�Ȃ�HP==0�ŏ�����
}

// bomb�̈ړ�����
void moveBomb( OBJ bomb[], OBJ player ) { // 2020/03/16
    inti;
    for( i = 0; i < Bomb_N; i++ ) { //�����𒼐i������

        //���e��`�悵�Ȃ��Ƃ�
        if( bomb[ i ].HP == 0 ) {
            // player�̌��ݒn�Ƀ��Z�b�g
            bomb[ i ].x = player.x;
            bomb[ i ].y = player.y;
            continue;
        }
        //���e����ʂ𒴂�����
        else if( bomb[ i ].x >= 470 && bomb[ i ].HP == 1 ) {
            // player�̌��ݒn�Ƀ��Z�b�g
            bomb[ i ].x  = player.x;
            bomb[ i ].y  = player.y;
            bomb[ i ].HP = 0; //�`�悵�Ȃ�
            continue;
        }
        //����ȊO�iHP==1)�̎�
        bomb[ i ] = move( bomb[ i ] ); //������
    }
}

// Bomb�̕`��
void drawBomb( OBJ bomb[] ) {
    int i;
    for( i = 0; i < Bomb_N; i++ ) {
        if( bomb[ i ].HP == 0 ) continue;
        lcd_drw_ell( pfb_b, bomb[ i ].x, bomb[ i ].y, bomb[ i ].w, bomb[ i ].h,
                     RGB( 5, 10, 5 ), 1 );
    }
}

// Enemy�̏�����
void initEnemy( OBJ enemy[] ) {
    int i;
    for( i = 0; i < Enemy_N; i++ )
        enemy[ i ] =
            initOBJ( rand() % 200 + 250, rand() % 250 + 60, rand() % 50 + 20,
                     10, rand() % 4 + 1, rand() % 3 - 1, Enemy_HP );
}

// Enemy�L�����̕`��
void drawEnemy( OBJ enemy[] ) {
    int i;
    int cnt = 1;
    for( i = 0; i < Enemy_N; i++ ) {
        if( enemy[ i ].HP <= 0 ) continue; //����ł鎞�͕`�悵�Ȃ�
        lcd_drw_rec( pfb_b, enemy[ i ].x - 13, enemy[ i ].y - 18,
                     enemy[ i ].x + 13, enemy[ i ].y,
                     RGB( 10, 15, 15 - 2 * enemy[ i ].HP ), 1 );
        lcd_drw_ell( pfb_b, enemy[ i ].x, enemy[ i ].y, enemy[ i ].w,
                     enemy[ i ].h, RGB( 10, 14, 15 - 2 * enemy[ i ].HP ), 1 );
    }
}

// Enemy�L�����̈ړ�����
void moveEnemy( OBJ enemy[] ) {
    int i;
    for( i = 0; i < Enemy_N; i++ ) { //�����\��(�㉺�ɓ����Ĕ�����j
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

//�Ԃ��������ۂ��̔���
int hit( OBJ bomb, OBJ enemy ) {
    if( abs( bomb.x - enemy.x ) <= 12 && abs( bomb.y - enemy.y ) <= 15 )
        return 1;
    else
        return 0;
}

//�G�ƂԂ��������̏���
void check_Player_Enemy( OBJ player, OBJ enemy[] ) {
    int i;
    for( i = 0; i < Enemy_N; i++ ) {
        if( hit( player, enemy[ i ] ) == 1 &&
            enemy[ i ].HP >= 1 ) { // hit�����Ƃ�
            player.HP -= 2;        // playerHP���Q���炷
            enemy[ i ].HP--;       //�GHP���P���炷
        }
    }
}

//���e�������������ۂ��̔���܂Ƃ�
void check_Bomb_Enemy( OBJ bomb[], OBJ enemy[] ) {
    int i, j;
    for( i = 0; i < Bomb_N; i++ ) {
        for( j = 0; j < Enemy_N; j++ ) {
            if( hit( bomb[ i ], enemy[ j ] ) == 1 &
                enemy[ j ].HP >= 1 ) { // hit�����Ƃ�
                if( bomb[ i ].HP == 1 ) {
                    bomb[ i ].HP = 0; //�{����`�悵�Ȃ�
                    enemy[ j ].HP--;  // HP�����炷
                }
            }
        }
    }
}

//�N���A�����𖞂����Ă��邩�`�F�b�N����
int clearCheck( OBJ enemy[] ) {
    int i;
    for( i = 0; i < Enemy_N; i++ ) {
        if( enemy[ i ].HP >= 1 ) return 0;
    }
    return 1;
}

//�f�o�b�O�p�֐�
void E( int i ) { printf( "error%d!\n", i ); }
