#undef _GLIBCXX_DEBUG                // disable run-time bound checking, etc
#pragma GCC optimize("Ofast,inline") // Ofast = O3,fast-math,allow-store-data-races,no-protect-parens

#pragma GCC target("bmi,bmi2,lzcnt,popcnt")                      // bit manipulation
#pragma GCC target("movbe")                                      // byte swap
#pragma GCC target("aes,pclmul,rdrnd")                           // encryption
#pragma GCC target("avx,avx2,f16c,fma,sse2,sse3,ssse3,sse4.1,sse4.2") // SIMD
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <limits.h>

#define MAX_WIDTH  20
#define MAX_HEIGHT 20
#define MAX_AGENTS 10
#define MAX_BEST_CMD_PER_AGENTS 3
#define MAX_PLAYERS 2
#define MAX_MOVES_PER_AGENT 5
#define MAX_SHOOTS_PER_AGENT 5
#define MAX_BOMB_PER_AGENT 5
#define MAX_COMMANDS_PER_AGENT 35
#define MAX_COMMANDS_PER_PLAYER 1024
#define MAX_SIMULATIONS 1024

// ==========================
// === DATA MODELS
// ==========================

typedef enum {
    CMD_SHOOT,
    CMD_THROW,
    CMD_HUNKER
} ActionType;
typedef struct {
    int target_x_or_id, target_y;
    float score; // utile pour trier les actions
} AgentAction;
typedef struct {
    int mv_x, mv_y;
    ActionType action_type;
    int target_x_or_id, target_y;
    float score; // utile pour trier les commandes
} AgentCommand;

typedef struct {
    float score;
    int my_cmds_index;
    int op_cmds_index;
} SimulationResult;

typedef struct {
    // [agent_id][y][x] = distance depuis agent_id à (x, y)
    int bfs_enemy_distances[MAX_AGENTS][MAX_HEIGHT][MAX_WIDTH];


    // Listes triées des meilleurs actions par agent
    AgentAction moves[MAX_AGENTS][MAX_MOVES_PER_AGENT];
    int move_counts[MAX_AGENTS];
    AgentAction shoots[MAX_AGENTS][MAX_SHOOTS_PER_AGENT];
    int shoot_counts[MAX_AGENTS];
    AgentAction bombs[MAX_AGENTS][MAX_BOMB_PER_AGENT];
    int bomb_counts[MAX_AGENTS];

    // Liste des commandes fusionnées (move+shoot+bomb+hunker) par agent
    AgentCommand agent_commands[MAX_AGENTS][MAX_COMMANDS_PER_AGENT];
    int agent_command_counts[MAX_AGENTS];

    // Liste des commandes fusionnées (combinaisons multi-agents) par joueur
    AgentCommand player_commands[MAX_PLAYERS][MAX_COMMANDS_PER_PLAYER][MAX_AGENTS];
    int player_command_count[MAX_PLAYERS];

    // Résultats de simulations triée par score pour obtenir la meilleur commande simulation_results[0].my_cmds_index
    SimulationResult simulation_results[MAX_SIMULATIONS];
    int simulation_count;
} GameOutput;

typedef struct {
    int id;
    int player_id;
    int shoot_cooldown;
    int optimal_range;
    int soaking_power;
    int splash_bombs;
} AgentInfo;
typedef struct {
    int agent_count;         // nombre d'agents pour ce joueur
    int agent_start_index;   // index de début dans agent_info[]
    int agent_stop_index;
} PlayerAgentInfo;

typedef struct {
    int x, y;
    int type;
} Tile;

typedef struct {
    int id;
    int x, y;
    int cooldown;
    int splash_bombs;
    int wetness;
    int alive;
} AgentState;

typedef struct {
    int width, height;
    Tile map[MAX_HEIGHT][MAX_WIDTH];
} MapInfo;

typedef struct {
    const int my_player_id;
    const int agent_info_count;
    AgentInfo agent_info[MAX_AGENTS];
    PlayerAgentInfo player_info[MAX_PLAYERS];
    MapInfo map;
} GameConstants;

typedef struct {
    AgentState agents[MAX_AGENTS];
    int agent_count_do_not_use; // use alive instead
    int my_agent_count_do_not_use; // use alive instead
} GameState;

typedef struct {
    GameConstants consts;
    GameState state;
    GameOutput output;
} GameInfo;

GameInfo game = {0};

// ==========================
// === UTILITAIRES
// ==========================
static clock_t gCPUStart;
#define CPU_RESET        (gCPUStart = clock())
#define CPU_MS_USED      (((double)(clock() - gCPUStart)) * 1000.0 / CLOCKS_PER_SEC)
#define CPU_BREAK(val)   if (CPU_MS_USED > (val)) break;
#define ERROR(text) {fprintf(stderr,"ERROR:%s",text);fflush(stderr);exit(1);}
#define ERROR_INT(text,val) {fprintf(stderr,"ERROR:%s:%d",text,val);fflush(stderr);exit(1);}

int controlled_score_gain_if_agent_moves_to(int agent_id, int nx, int ny) {
    // Calcule le gain net de zone contrôlée si l'agent se déplace en (nx, ny)
    int my_gain = 0;
    int enemy_gain = 0;

    for (int y = 0; y < game.consts.map.height; y++) {
        for (int x = 0; x < game.consts.map.width; x++) {
            if (game.consts.map.map[y][x].type > 0) continue; // obstacle

            int d_my = INT_MAX;
            int d_en = INT_MAX;

            for (int i = game.consts.player_info[game.consts.my_player_id].agent_start_index;
                 i <= game.consts.player_info[game.consts.my_player_id].agent_stop_index; i++) {
                if (!game.state.agents[i].alive) continue;
                int ax = (i == agent_id) ? nx : game.state.agents[i].x;
                int ay = (i == agent_id) ? ny : game.state.agents[i].y;
                int d = abs(x - ax) + abs(y - ay);
                if (game.state.agents[i].wetness >= 50) d *= 2;
                if (d < d_my) d_my = d;
            }

            for (int i = game.consts.player_info[!game.consts.my_player_id].agent_start_index;
                 i <= game.consts.player_info[!game.consts.my_player_id].agent_stop_index; i++) {
                if (!game.state.agents[i].alive) continue;
                int ax = game.state.agents[i].x;
                int ay = game.state.agents[i].y;
                int d = abs(x - ax) + abs(y - ay);
                if (game.state.agents[i].wetness >= 50) d *= 2;
                if (d < d_en) d_en = d;
            }

            if (d_my < d_en) my_gain++;
            else if (d_en < d_my) enemy_gain++;
        }
    }

    return my_gain - enemy_gain;
}


// ==========================
// === MAIN FUNCTIONS
// ==========================

void read_game_inputs_init() {
    int my_id, agent_info_count;
    scanf("%d", &my_id);
    scanf("%d", &agent_info_count);

    *(int*)&game.consts.my_player_id = my_id;
    *(int*)&game.consts.agent_info_count = agent_info_count;

    for (int i = 0; i < agent_info_count; i++) {
        scanf("%d%d%d%d%d%d",
              &game.consts.agent_info[i].id,
              &game.consts.agent_info[i].player_id,
              &game.consts.agent_info[i].shoot_cooldown,
              &game.consts.agent_info[i].optimal_range,
              &game.consts.agent_info[i].soaking_power,
              &game.consts.agent_info[i].splash_bombs);
    }

    // Initialiser les infos par joueur
    for (int p = 0; p < MAX_PLAYERS; ++p) {
        game.consts.player_info[p].agent_count = 0;
        game.consts.player_info[p].agent_start_index = -1;
        game.consts.player_info[p].agent_stop_index = -1;
    }

    // Scanner les agents pour remplir les infos par player
    // les agents sont contigus dans la structure
    for (int i = 0; i < game.consts.agent_info_count; ++i) {
        int player = game.consts.agent_info[i].player_id;
        if (game.consts.player_info[player].agent_count == 0) {
            game.consts.player_info[player].agent_start_index = i;
        }
        game.consts.player_info[player].agent_count++;
        game.consts.player_info[player].agent_stop_index = i;
    }
    fprintf(stderr,"%d %d %d\n",game.consts.player_info[0].agent_count,game.consts.player_info[0].agent_start_index,game.consts.player_info[0].agent_stop_index);
    fprintf(stderr,"%d %d %d\n",game.consts.player_info[1].agent_count,game.consts.player_info[1].agent_start_index,game.consts.player_info[1].agent_stop_index);
    scanf("%d%d", &game.consts.map.width, &game.consts.map.height);
    for (int i = 0; i < game.consts.map.height * game.consts.map.width; i++) {
        int x, y, tile_type;
        scanf("%d%d%d", &x, &y, &tile_type);
        game.consts.map.map[y][x] = (Tile){x, y, tile_type};
    }
}

void read_game_inputs_cycle() {    
    // Réinitialiser tous les agents a dead
    for (int i = 0; i < MAX_AGENTS; i++) {
        game.state.agents[i].alive = 0;
    }
    scanf("%d", &game.state.agent_count_do_not_use);
    int agent_id,agent_x,agent_y,agent_cooldown,agent_splash_bombs,agent_wetness;
    for (int i = 0; i < game.state.agent_count_do_not_use; i++) {
        scanf("%d%d%d%d%d%d",
              &agent_id,
              &agent_x,
              &agent_y,
              &agent_cooldown,
              &agent_splash_bombs,
              &agent_wetness);
        // index start at 1
        agent_id = agent_id -1;
        game.state.agents[agent_id] = (AgentState){agent_id,agent_x,agent_y,agent_cooldown,agent_splash_bombs,agent_wetness,1};
    }
    
    scanf("%d", &game.state.my_agent_count_do_not_use);
    CPU_RESET;
}

void precompute_bfs_distances() {
    static const int dirs[4][2] = {{0,1},{1,0},{0,-1},{-1,0}};

    for (int k = 0; k < MAX_AGENTS; k++) {
        AgentState* enemy = &game.state.agents[k];
        if (!enemy->alive) continue;

        int eid = enemy->id;
        int visited[MAX_HEIGHT][MAX_WIDTH] = {0};
        int dist[MAX_HEIGHT][MAX_WIDTH] = {0};

        int queue_x[MAX_WIDTH * MAX_HEIGHT];
        int queue_y[MAX_WIDTH * MAX_HEIGHT];
        int front = 0, back = 0;

        visited[enemy->y][enemy->x] = 1;
        dist[enemy->y][enemy->x] = 0;
        queue_x[back] = enemy->x;
        queue_y[back++] = enemy->y;

        while (front < back) {
            int x = queue_x[front];
            int y = queue_y[front++];
            for (int d = 0; d < 4; d++) {
                int nx = x + dirs[d][0];
                int ny = y + dirs[d][1];
                if (nx < 0 || nx >= game.consts.map.width || ny < 0 || ny >= game.consts.map.height)
                    continue;
                if (game.consts.map.map[ny][nx].type > 0) continue; // obstacle
                if (visited[ny][nx]) continue;

                visited[ny][nx] = 1;
                dist[ny][nx] = dist[y][x] + 1;

                queue_x[back] = nx;
                queue_y[back++] = ny;
            }
        }

        // Stocker dans la sortie
        for (int y = 0; y < game.consts.map.height; y++) {
            for (int x = 0; x < game.consts.map.width; x++) {
                if (!visited[y][x])
                    game.output.bfs_enemy_distances[eid][y][x] = 9999;
                else
                    game.output.bfs_enemy_distances[eid][y][x] = dist[y][x];
            }
        }
    }
}




void compute_best_agents_moves(int agent_id) {
    static const int dirs[5][2] = {
        {0, 0},   // stay in place
        {-1, 0},  // left
        {1, 0},   // right
        {0, -1},  // up
        {0, 1}    // down
    };

    AgentState* agent_state = &game.state.agents[agent_id];
    AgentInfo* agent_info   = &game.consts.agent_info[agent_id];

    game.output.move_counts[agent_id] = 0;

    int my_player_id = agent_info->player_id;
    int enemy_player_id = !my_player_id;

    int enemy_start = game.consts.player_info[enemy_player_id].agent_start_index;
    int enemy_stop  = game.consts.player_info[enemy_player_id].agent_stop_index;

    int ally_start = game.consts.player_info[my_player_id].agent_start_index;
    int ally_stop  = game.consts.player_info[my_player_id].agent_stop_index;

    // Étape 1 : Y a-t-il un ennemi dangereux proche ?
    bool danger = false;
    for (int k = enemy_start; k <= enemy_stop; k++) {
        AgentState* enemy = &game.state.agents[k];
        if (!enemy->alive) continue;
        if (enemy->splash_bombs <= 0) continue;

        int dist = abs(enemy->x - agent_state->x) + abs(enemy->y - agent_state->y);
        if (dist <= 7) {
            danger = true;
            break;
        }
    }

    // Étape 2 : Générer les mouvements possibles
    for (int d = 0; d < 5; d++) {
        int nx = agent_state->x + dirs[d][0];
        int ny = agent_state->y + dirs[d][1];

        if (nx < 0 || nx >= game.consts.map.width || ny < 0 || ny >= game.consts.map.height) continue;
        if (game.consts.map.map[ny][nx].type > 0) continue;

        int min_dist_to_enemy = 9999;
        for (int k = enemy_start; k <= enemy_stop; k++) {
            AgentState* op_state = &game.state.agents[k];
            if (!op_state->alive) continue;

            int dist = game.output.bfs_enemy_distances[op_state->id][ny][nx];
            if (dist < min_dist_to_enemy) min_dist_to_enemy = dist;
        }

        float penalty = 0.0f;
        if (danger) {
            for (int a = ally_start; a <= ally_stop; a++) {
                if (a == agent_id) continue;
                AgentState* ally = &game.state.agents[a];
                if (!ally->alive) continue;

                int dist_ally = abs(ally->x - nx) + abs(ally->y - ny);
                if (dist_ally < 3) {
                    penalty += 20.0f;
                }
            }
        }

        int gain = controlled_score_gain_if_agent_moves_to(agent_id, nx, ny);
        float score = (float)gain*10 + (-min_dist_to_enemy - penalty);

        AgentAction action = {
            .target_x_or_id = nx,
            .target_y = ny,
            .score = score
        };

        if (game.output.move_counts[agent_id] < MAX_MOVES_PER_AGENT) {
            game.output.moves[agent_id][game.output.move_counts[agent_id]++] = action;
        }
    }

    // Tri décroissant du score
    for (int m = 0; m < game.output.move_counts[agent_id] - 1; m++) {
        for (int n = m + 1; n < game.output.move_counts[agent_id]; n++) {
            if (game.output.moves[agent_id][n].score > game.output.moves[agent_id][m].score) {
                AgentAction tmp = game.output.moves[agent_id][m];
                game.output.moves[agent_id][m] = game.output.moves[agent_id][n];
                game.output.moves[agent_id][n] = tmp;
            }
        }
    }
}


void compute_best_agents_shoot(int agent_id,int new_shooter_x,int new_shooter_y) {

    AgentState* shooter_state = &game.state.agents[agent_id];
    AgentInfo* shooter_info   = &game.consts.agent_info[agent_id];
    AgentAction * output_list = &game.output.shoots[agent_id][0];

    game.output.shoot_counts[agent_id] = 0;
    if (shooter_state->cooldown > 0) return;

    int my_player_id = shooter_info->player_id;
    int enemy_player_id = !my_player_id;
    int enemy_start = game.consts.player_info[enemy_player_id].agent_start_index;
    int enemy_stop  = game.consts.player_info[enemy_player_id].agent_stop_index;

    int shoots_count = 0;

    for (int k = enemy_start; k <= enemy_stop; k++) {
        AgentState* enemy = &game.state.agents[k];
        if (!enemy->alive) continue;
        

        int dx = abs(enemy->x - new_shooter_x);
        int dy = abs(enemy->y - new_shooter_y);
        int dist = dx + dy;

        int max_range = 2 * shooter_info->optimal_range;

        if (dist > max_range) continue; // tir impossible

        // Bonus si proche de la portée optimale
        float optimal_bonus = (dist <= shooter_info->optimal_range)
            ? 1.5f
            : 1.0f;

        // Score = wetness priorité + proximité portée
        float score = enemy->wetness * optimal_bonus - dist * 2;

        AgentAction shoot = {
            .target_x_or_id = k, // ID de l’ennemi
            .target_y = 0, // unused
            .score = score
        };
        output_list[shoots_count++] = shoot;
    }

    // Tri des tirs par score décroissant
    for (int m = 0; m < shoots_count - 1; m++) {
        for (int n = m + 1; n < shoots_count; n++) {
            if (output_list[n].score > output_list[m].score) {
                AgentAction tmp = output_list[m];
                output_list[m] = output_list[n];
                output_list[n] = tmp;
            }
        }
    }
    game.output.shoot_counts[agent_id] = shoots_count;
    
}
void compute_best_agents_bomb(int agent_id,int new_thrower_x,int new_thrower_y) {
    game.output.bomb_counts[agent_id] = 0;

    AgentState* thrower_state = &game.state.agents[agent_id];
    AgentInfo* thrower_info   = &game.consts.agent_info[agent_id];
    if (!thrower_state->alive || thrower_state->splash_bombs <= 0) return;

    int my_player_id = thrower_info->player_id;
    int enemy_player_id = !my_player_id;

    // Parcours des ennemis vivants
    for (int k = game.consts.player_info[enemy_player_id].agent_start_index;
            k <= game.consts.player_info[enemy_player_id].agent_stop_index; ++k) {

        AgentState* enemy = &game.state.agents[k];
        if (!enemy->alive) continue;

        int tx = enemy->x;
        int ty = enemy->y;

        // Vérifier portée
        int dist = abs(tx - new_thrower_x) + abs(ty - new_thrower_y);
        if (dist > 4) continue;

        // Vérifier que la bombe ne touche pas un allié ni le lanceur
        bool hits_ally_or_self = false;
        for (int a = game.consts.player_info[my_player_id].agent_start_index;
                a <= game.consts.player_info[my_player_id].agent_stop_index; ++a) {
            AgentState* ally = &game.state.agents[a];
            if (!ally->alive) continue;

            int dxa = abs(ally->x - tx);
            int dya = abs(ally->y - ty);
            if (dxa <= 1 && dya <= 1) {
                hits_ally_or_self = true;
                break;
            }
        }

        if (hits_ally_or_self) continue;

        if (game.output.bomb_counts[agent_id] < MAX_BOMB_PER_AGENT) {
            game.output.bombs[agent_id][game.output.bomb_counts[agent_id]++] = (AgentAction){
                .target_x_or_id = tx,
                .target_y = ty,
                .score = 100 - enemy->wetness
            };
        }
    }

    // Tri décroissant par score
    int count = game.output.bomb_counts[agent_id];
    for (int m = 0; m < count - 1; m++) {
        for (int n = m + 1; n < count; n++) {
            if (game.output.bombs[agent_id][n].score > game.output.bombs[agent_id][m].score) {
                AgentAction tmp = game.output.bombs[agent_id][m];
                game.output.bombs[agent_id][m] = game.output.bombs[agent_id][n];
                game.output.bombs[agent_id][n] = tmp;
            }
        }
    }
}



void compute_best_agents_commands() {

    

    for (int i = 0; i < MAX_AGENTS; i++) {

        game.output.agent_command_counts[i]=0;        
        if(!game.state.agents[i].alive) continue;
        int cmd_index = 0;
        
        compute_best_agents_moves(i);     

        int move_count= game.output.move_counts[i];
        

        for (int m = 0; m < move_count && cmd_index < MAX_COMMANDS_PER_AGENT; m++) {
            AgentAction* mv = &game.output.moves[i][m];
            int mv_x = mv->target_x_or_id;
            int mv_y = mv->target_y;


            compute_best_agents_bomb(i,mv_x,mv_y);
            // 2. Bombe (max 1)
            if (game.output.bomb_counts[i] > 0 && cmd_index < MAX_COMMANDS_PER_AGENT) {
                AgentAction* bomb = &game.output.bombs[i][0]; // meilleure bombe
                game.output.agent_commands[i][cmd_index++] = (AgentCommand){
                    .mv_x = mv_x,
                    .mv_y = mv_y,
                    .action_type = CMD_THROW,
                    .target_x_or_id = bomb->target_x_or_id,
                    .target_y = bomb->target_y,
                    .score = bomb->score
                };
            }

            compute_best_agents_shoot(i,mv_x,mv_y);
            // 1. Shoots (jusqu’à 5)
            int shoot_limit = game.output.shoot_counts[i];
            if (shoot_limit > MAX_SHOOTS_PER_AGENT) shoot_limit = MAX_SHOOTS_PER_AGENT;
            for (int s = 0; s < shoot_limit && cmd_index < MAX_COMMANDS_PER_AGENT; s++) {
                AgentAction* shoot = &game.output.shoots[i][s];
                game.output.agent_commands[i][cmd_index++] = (AgentCommand){
                    .mv_x = mv_x,
                    .mv_y = mv_y,
                    .action_type = CMD_SHOOT,
                    .target_x_or_id = shoot->target_x_or_id,
                    .target_y = shoot->target_y,
                    .score = shoot->score
                };
            }
            // 3. Hunker
            if (cmd_index < MAX_COMMANDS_PER_AGENT) {
                game.output.agent_commands[i][cmd_index++] = (AgentCommand){
                    .mv_x = mv_x,
                    .mv_y = mv_y,
                    .action_type = CMD_HUNKER,
                    .target_x_or_id = -1,
                    .target_y = -1,
                    .score = mv->score
                };
            }
        }
        game.output.agent_command_counts[i] = cmd_index;
    }
}

void compute_best_player_commands() {
    for (int p = 0; p < MAX_PLAYERS; p++) {
        // On récupère les agents du joueur p
        game.output.player_command_count[p] = 0;

        int agent_start_id = game.consts.player_info[p].agent_start_index;
        int agent_stop_id = game.consts.player_info[p].agent_stop_index;

        // Produit cartésien des commandes agents pour former les commandes joueur
        int indices[MAX_AGENTS];
        memset(indices, 0, sizeof(indices));

        while (true) {
            if (game.output.player_command_count[p] >= MAX_COMMANDS_PER_PLAYER) ERROR_INT("ERROR to many command",MAX_COMMANDS_PER_PLAYER)

            // Construire une commande complète (tableau de AgentCommand d’agents)
            for (int agent_id = agent_start_id; agent_id <= agent_stop_id; agent_id++) {
                if(!game.state.agents[agent_id].alive) continue;
                int cmd_id = indices[agent_id];
                game.output.player_commands[p][game.output.player_command_count[p]][agent_id] = game.output.agent_commands[agent_id][cmd_id];
            }

            game.output.player_command_count[p]++;

            // Incrémenter les indices pour la combinaison suivante
            int carry = 1;
            for (int agent_id = agent_start_id; agent_id <= agent_stop_id && carry; agent_id++) {
                if(!game.state.agents[agent_id].alive) continue;
                indices[agent_id]++;
                int cur_index = indices[agent_id];
                if (cur_index >= MAX_BEST_CMD_PER_AGENTS ||  cur_index >= game.output.agent_command_counts[agent_id]) {
                    indices[agent_id] = 0;
                    carry = 1;
                } else {
                    carry = 0;
                }
            }

            if (carry) break;  // on a parcouru toutes les combinaisons
        }
    }
}


void compute_evaluation() {
    int my_id = game.consts.my_player_id;
    int my_start = game.consts.player_info[my_id].agent_start_index;
    int my_stop  = game.consts.player_info[my_id].agent_stop_index;

    game.output.simulation_count = 0;

    // For all my cmd to test // todo min max enemie
    int count = game.output.player_command_count[my_id];
    for (int i = 0; i < count; i++) {
        // État simulé des agents
        AgentState sim_agents[MAX_AGENTS];
        memcpy(sim_agents, game.state.agents, sizeof(sim_agents));

        int wetness_gain = 0;
        int nb_50_wet_gain = 0;
        int nb_100_wet_gain = 0;

        // === Étape 1: appliquer les mouvements ===
        for (int aid = my_start; aid <= my_stop; aid++) {
            if (!sim_agents[aid].alive) continue;
            AgentCommand* cmd = &game.output.player_commands[my_id][i][aid];
            sim_agents[aid].x = cmd->mv_x;
            sim_agents[aid].y = cmd->mv_y;
        }

        // === Étape 2: bombes/shoot===
        for (int aid = my_start; aid <= my_stop; aid++) {
            if (!sim_agents[aid].alive) continue;
            AgentCommand* cmd = &game.output.player_commands[my_id][i][aid];
            if (cmd->action_type == CMD_THROW)
            {
                for (int t = 0; t < MAX_AGENTS; t++) {
                    if (!sim_agents[t].alive) continue;
                    int dx = abs(sim_agents[t].x - cmd->target_x_or_id);
                    int dy = abs(sim_agents[t].y - cmd->target_y);
                    if (dx <= 1 && dy <= 1)  sim_agents[t].wetness += 30;
                }
            }       
            if (cmd->action_type == CMD_SHOOT)
            {
                int target_id = cmd->target_x_or_id;
                sim_agents[target_id].wetness += game.consts.agent_info[aid].soaking_power/2.0;
                // simulate properly shoot
            }
        }
   

        // === Étape 3: kpi dead ===
        for (int aid = 0; aid < MAX_AGENTS; aid++) {
            int curr_wet = game.state.agents[aid].wetness;
            int new_wet = sim_agents[aid].wetness;
            if(new_wet>=100)
            {
                new_wet=100;
                sim_agents[aid].alive=0;
            }
            int pId = game.consts.agent_info[aid].player_id;
            int delta_wet = new_wet-curr_wet;
            if(delta_wet==0) continue;
            if(new_wet>=100) {nb_100_wet_gain=(my_id==pId)?(nb_100_wet_gain-1):(nb_100_wet_gain+1);}
            if(new_wet>=50) {nb_50_wet_gain=(my_id==pId)?(nb_50_wet_gain-1):(nb_50_wet_gain+1);}
            wetness_gain = (my_id==pId)?(wetness_gain-delta_wet):(wetness_gain+delta_wet);
        }

        // === Étape 5: score de contrôle de zone ===
        int control_score = 0;
        for (int aid = my_start; aid <= my_stop; aid++) {
            if (!sim_agents[aid].alive) continue;
            control_score += controlled_score_gain_if_agent_moves_to(aid, sim_agents[aid].x, sim_agents[aid].y);
        }

        // === Étape 6: évaluation finale ===
        float score =
            control_score * 10.0f +
            wetness_gain * 1.5f +
            nb_50_wet_gain * 20.0f +
            nb_100_wet_gain * 30.0f ;

        game.output.simulation_results[game.output.simulation_count++] = (SimulationResult){
            .score = score,
            .my_cmds_index = i,
            .op_cmds_index = -1 // ignoré pour l’instant
        };
    }

    // Tri décroissant des résultats
    for (int a = 0; a < game.output.simulation_count - 1; a++) {
        for (int b = a + 1; b < game.output.simulation_count; b++) {
            if (game.output.simulation_results[b].score > game.output.simulation_results[a].score) {
                SimulationResult tmp = game.output.simulation_results[a];
                game.output.simulation_results[a] = game.output.simulation_results[b];
                game.output.simulation_results[b] = tmp;
            }
        }
    }
}





void apply_output() {
    float cpu=CPU_MS_USED;
    int my_player_id = game.consts.my_player_id;
    int agent_start_id = game.consts.player_info[my_player_id].agent_start_index;
    int agent_stop_id = game.consts.player_info[my_player_id].agent_stop_index;

    if (game.output.simulation_count == 0) {
       ERROR("no simu result");
    }

    int best_index = game.output.simulation_results[0].my_cmds_index;


    for (int agent_id = agent_start_id; agent_id <= agent_stop_id; agent_id++) {
        if(!game.state.agents[agent_id].alive) continue;
        AgentCommand* cmd = &game.output.player_commands[my_player_id][best_index][agent_id];

        // Commencer par agentId+1 car le jeu attend un index demarrage en 1
        printf("%d", agent_id+1);

        // Déplacement, si différent de la position actuelle
        if (cmd->mv_x != game.state.agents[agent_id].x || cmd->mv_y != game.state.agents[agent_id].y) {
            printf(";MOVE %d %d", cmd->mv_x, cmd->mv_y);
        }

        // Action de combat (SHOOT, THROW ou HUNKER_DOWN)
        if (cmd->action_type == CMD_SHOOT) {
            printf(";SHOOT %d",cmd->target_x_or_id +1); // ici on utilise l'id +1);
        } else if (cmd->action_type == CMD_THROW) {
            printf(";THROW %d %d", cmd->target_x_or_id, cmd->target_y);
        } else if (cmd->action_type == CMD_HUNKER) {
            printf(";HUNKER_DOWN");
        } else {
            // Si pas d’action combat, rien à ajouter
        }

        // Optionnel : message de debug (par exemple)
        printf(";MESSAGE %.2fms",cpu);

        printf("\n");
        fflush(stdout);
    }
}




// ==========================
// === MAIN LOOP
// ==========================

int main() {
    read_game_inputs_init();

    while (1) {
        fprintf(stderr,"current\n");
        // ========== Lecture des entrées
        read_game_inputs_cycle();

        // ========== Liste des meilleures commandes par agent ==========
        precompute_bfs_distances();
        compute_best_agents_commands();

        // ========== Combinaisons possibles entre agents ==========
        compute_best_player_commands();

        // ========== Évaluation stratégique ==========
        compute_evaluation();

        // ========== Application ==========
        apply_output();
    }

    return 0;
}
