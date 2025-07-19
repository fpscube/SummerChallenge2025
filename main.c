#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>

#define MAX_WIDTH  20
#define MAX_HEIGHT 20
#define MAX_AGENTS 10
#define MAX_BEST_CMD_PER_AGENTS 2
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
    int agent_count;
    AgentState agents[MAX_AGENTS];
    int my_agent_count;
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
    scanf("%d", &game.state.agent_count);
    int agent_id,agent_x,agent_y,agent_cooldown,agent_splash_bombs,agent_wetness;
    for (int i = 0; i < game.state.agent_count; i++) {
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

    scanf("%d", &game.state.my_agent_count);
    CPU_RESET;
}

void compute_best_agents_moves() {
    static const int dirs[5][2] = {
        {0, 0},   // stay in place
        {-1, 0},  // left
        {1, 0},   // right
        {0, -1},  // up
        {0, 1}    // down
    };

    for (int i = 0; i < game.state.agent_count; i++) {
        AgentState* agent_state = &game.state.agents[i];
        AgentInfo* agent_info   = &game.consts.agent_info[i];

        game.output.move_counts[i] = 0;
        if (!agent_state->alive) continue;

        int my_player_id = agent_info->player_id;
        int enemy_player_id = !my_player_id;
        int enemy_start = game.consts.player_info[enemy_player_id].agent_start_index;
        int enemy_stop  = game.consts.player_info[enemy_player_id].agent_stop_index;

        for (int d = 0; d < 5; d++) {
            int nx = agent_state->x + dirs[d][0];
            int ny = agent_state->y + dirs[d][1];

            if (nx < 0 || nx >= game.consts.map.width || ny < 0 || ny >= game.consts.map.height) continue;
            if (game.consts.map.map[ny][nx].type > 0) continue;

            int min_dist = 9999;

            for (int k = enemy_start; k <= enemy_stop; k++) {
                AgentState* op_state = &game.state.agents[k];
                if (!op_state->alive) continue;

                int dist = abs(op_state->x - nx) + abs(op_state->y - ny);
                if (dist < min_dist) min_dist = dist;
            }

            AgentAction action = {
                .target_x_or_id = nx,
                .target_y = ny,
                .score = -min_dist
            };

            if (game.output.move_counts[i] < MAX_MOVES_PER_AGENT) {
                game.output.moves[i][game.output.move_counts[i]++] = action;
            }
        }

        // Tri décroissant du score (meilleure proximité d'abord)
        for (int m = 0; m < game.output.move_counts[i] - 1; m++) {
            for (int n = m + 1; n < game.output.move_counts[i]; n++) {
                if (game.output.moves[i][n].score > game.output.moves[i][m].score) {
                    AgentAction tmp = game.output.moves[i][m];
                    game.output.moves[i][m] = game.output.moves[i][n];
                    game.output.moves[i][n] = tmp;
                }
            }
        }
    }
}


void compute_best_agents_shoot() {
    for (int i = 0; i < game.state.agent_count; i++) {
        AgentState* shooter_state = &game.state.agents[i];
        AgentInfo* shooter_info   = &game.consts.agent_info[i];
        AgentAction * output_list = &game.output.shoots[i][0];

        game.output.shoot_counts[i] = 0;
        if (!shooter_state->alive || shooter_state->cooldown > 0) continue;

        int my_player_id = shooter_info->player_id;
        int enemy_player_id = !my_player_id;
        int enemy_start = game.consts.player_info[enemy_player_id].agent_start_index;
        int enemy_stop  = game.consts.player_info[enemy_player_id].agent_stop_index;

        int shoots_count = 0;

        for (int k = enemy_start; k <= enemy_stop; k++) {
            AgentState* enemy = &game.state.agents[k];
            if (!enemy->alive) continue;

            int dx = abs(enemy->x - shooter_state->x);
            int dy = abs(enemy->y - shooter_state->y);
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
        game.output.shoot_counts[i] = shoots_count;
    }
}

void compute_best_agents_bomb() {
    for (int i = 0; i < game.state.agent_count; i++) {
        game.output.bomb_counts[i] = 0;

        AgentState* thrower_state = &game.state.agents[i];
        AgentInfo* thrower_info   = &game.consts.agent_info[i];
        if (!thrower_state->alive || thrower_state->splash_bombs <= 0) continue;

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
            int dist = abs(tx - thrower_state->x) + abs(ty - thrower_state->y);
            if (dist > 4) continue;

            bool has_ally = false;
            // Vérifie si un allié est présent dans la zone 3x3 centrée sur (tx, ty)
            for (int ox = -1; ox <= 1 && !has_ally; ox++) {
                for (int oy = -1; oy <= 1; oy++) {
                    int ax = tx + ox;
                    int ay = ty + oy;
                    if (ax < 0 || ax >= game.consts.map.width || ay < 0 || ay >= game.consts.map.height)
                        continue;

                    for (int j = game.consts.player_info[my_player_id].agent_start_index;
                             j <= game.consts.player_info[my_player_id].agent_stop_index; j++) {
                        if (!game.state.agents[j].alive) continue;
                        AgentState* ally = &game.state.agents[j];

                        // ⚠️ Ne pas bombarder un allié
                        if (ally->x == ax && ally->y == ay) {
                            has_ally = true;
                            break;
                        }
                    }

                    // ⚠️ Ne pas bombarder la position future du tireur
                    for (int m = 0; m < game.output.move_counts[i]; m++) {
                        AgentAction* mv = &game.output.moves[i][m];
                        if (mv->target_x_or_id == ax && mv->target_y == ay) {
                            has_ally = true;
                            break;
                        }
                    }
                }
            }

            if (has_ally) continue;

            // Score simple : 1 point par ennemi touché dans la zone
            int hits = 0;
            for (int ox = -1; ox <= 1; ox++) {
                for (int oy = -1; oy <= 1; oy++) {
                    int ax = tx + ox;
                    int ay = ty + oy;
                    if (ax < 0 || ax >= game.consts.map.width || ay < 0 || ay >= game.consts.map.height)
                        continue;

                    for (int e = game.consts.player_info[enemy_player_id].agent_start_index;
                             e <= game.consts.player_info[enemy_player_id].agent_stop_index; e++) {
                        if (!game.state.agents[e].alive) continue;
                        AgentState* target = &game.state.agents[e];
                        if (target->x == ax && target->y == ay) {
                            hits++;
                        }
                    }
                }
            }

            if (hits == 0) continue;

            float score = hits * 10.0f;

            if (game.output.bomb_counts[i] < MAX_BOMB_PER_AGENT) {
                game.output.bombs[i][game.output.bomb_counts[i]++] = (AgentAction){
                    .target_x_or_id = tx,
                    .target_y = ty,
                    .score = score
                };
            }
        }

        // Tri décroissant par score
        int count = game.output.bomb_counts[i];
        for (int m = 0; m < count - 1; m++) {
            for (int n = m + 1; n < count; n++) {
                if (game.output.bombs[i][n].score > game.output.bombs[i][m].score) {
                    AgentAction tmp = game.output.bombs[i][m];
                    game.output.bombs[i][m] = game.output.bombs[i][n];
                    game.output.bombs[i][n] = tmp;
                }
            }
        }
    }
}



void compute_best_agents_commands() {
    for (int i = 0; i < game.state.agent_count; i++) {

        game.output.agent_command_counts[i]=0;
        
        if(!game.state.agents[i].alive) continue;
        
        int cmd_index = 0;
        int move_count= game.output.move_counts[i];

        for (int m = 0; m < move_count && cmd_index < MAX_COMMANDS_PER_AGENT; m++) {
            AgentAction* mv = &game.output.moves[i][m];
            int mv_x = mv->target_x_or_id;
            int mv_y = mv->target_y;

            // 1. Bombe (max 1)
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

            // 2. Shoots (jusqu’à 5)
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
                int cmd_id = indices[agent_id];
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


void compute_minimax_evaluation() {
    // utilisation d'une liste dans le datamodel pour stocker les resultat de la simulation et les score
    // Minimax / early-prune / memoisation/eval
    // dans un premier temps 
    fprintf(stderr, "\n=== DEBUG: Minimax - Aperçu des premières commandes par joueur ===\n");
    int my_player_id = game.consts.my_player_id;
    for (int p = 0; p < MAX_PLAYERS; p++) {
        if(p==my_player_id)
            fprintf(stderr, "--- PLAYER ME ----\n");
        else
            fprintf(stderr, "--- PLAYER OP ----\n");

        int count = game.output.player_command_count[p];
        int to_show = (count < 2) ? count : 2;


        int agent_start_id = game.consts.player_info[p].agent_start_index;
        int agent_stop_id = game.consts.player_info[p].agent_stop_index;

        for (int i = 0; i < to_show; i++) {
            fprintf(stderr, "  Command set #%d:\n", i);
            for (int agent_id = agent_start_id; agent_id <= agent_stop_id; agent_id++) {
                if(!game.state.agents[agent_id].alive) continue;

                AgentCommand* cmd = &game.output.player_commands[p][i][agent_id];
                
                // Skip empty entries (agents non utilisés dans cette team)
                if (cmd->mv_x == 0 && cmd->mv_y == 0 && cmd->action_type == CMD_HUNKER && cmd->target_x_or_id == -1)
                    continue;

                const char* action_name = (cmd->action_type == CMD_HUNKER) ? "HUNKER" :
                                          (cmd->action_type == CMD_SHOOT)  ? "SHOOT" :
                                          (cmd->action_type == CMD_THROW)  ? "THROW" : "?";

                fprintf(stderr, "    [%d] %s  MV=(%d,%d)  ", agent_id, action_name, cmd->mv_x, cmd->mv_y);
                if (cmd->action_type == CMD_HUNKER) {
                    fprintf(stderr, "TARGET=(-,-)\n");
                } else {
                    fprintf(stderr, "TARGET=(%d,%d)\n", cmd->target_x_or_id, cmd->target_y);
                }
            }
        }
    }
    // === Stocker la première combinaison par défaut ===
    if (game.output.player_command_count[my_player_id] > 0 &&
        game.output.player_command_count[!my_player_id] > 0 &&
        game.output.simulation_count < MAX_SIMULATIONS) {

        game.output.simulation_results[0] = (SimulationResult){
            .score = 0.0f, // tu peux ajuster ici selon ton modèle plus tard
            .my_cmds_index = 0,
            .op_cmds_index = 0
        };

        game.output.simulation_count = 1;

        fprintf(stderr, "\n✔️  SimulationResult[0] initialisé avec le premier set de commandes.\n");
    } else {
        fprintf(stderr, "⚠️  Impossible d'initialiser SimulationResult[0] (listes vides).\n");
    }

    fprintf(stderr, "=========================================================\n");
}


void compute_beam_search_evaluation() {
    // Beam search profondeur N
}

void apply_output() {
    float cpu=CPU_MS_USED;
    int my_player_id = game.consts.my_player_id;
    int agent_start_id = game.consts.player_info[my_player_id].agent_start_index;
    int agent_stop_id = game.consts.player_info[my_player_id].agent_stop_index;

    if (game.output.simulation_count == 0) {
        // Pas de simulation, on ordonne juste HUNKER_DOWN sans déplacement pour tous les agents
        for (int i = 0; i < game.state.my_agent_count; i++) {
            printf("%d;HUNKER_DOWN", game.state.agents[i].id);
            printf(";MESSAGE %.2fms\n",cpu);
        }
        return;
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
    }
}




// ==========================
// === MAIN LOOP
// ==========================

int main() {
    read_game_inputs_init();

    while (1) {
        // ========== Step 0: Lecture des entrées
        read_game_inputs_cycle();

        // ========== Step 1: Actions élémentaires pour chaque agent ==========
        compute_best_agents_moves();
        compute_best_agents_shoot();
        compute_best_agents_bomb();

        // ========== Step 2: Liste des meilleures commandes par agent ==========
        compute_best_agents_commands();

        // ========== Step 3: Combinaisons possibles entre agents ==========
        compute_best_player_commands();

        // ========== Step 4: Évaluation stratégique ==========
        compute_minimax_evaluation();

        // ========== Step 5: Recherche anticipée ==========
        compute_beam_search_evaluation();

        // ========== Step 6: Application ==========
        apply_output();
    }

    return 0;
}
