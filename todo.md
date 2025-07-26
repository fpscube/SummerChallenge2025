
strategie bomb block
strategie escape from fire (when both can wire)

Analyse des combinatoires:
5 vs 5
3*3*3*3*3 * (4+3+1) = 1944

4 vs 4 => 224*16= 3600
4*4*4*4 * (3+3+1) = 1765

3 vs 3
5*5*5 *(3+3+1)= 975

2 vs 2
5mv * 5mv * (2 shot + 2 bomb + 1 wait) = 25*(5) = 125
* (2*2*(1+1+1))
=>1500

1 vs 1
5mv x (1 shot + 1 bomb + 1 wait) => 15 = full unit
minmax full => 15*15=225



Travailler les heuristique (important pour la simu enemie)

ajouter une map de danger bomb enemie à ponderer 
=> danger bombe (tous les agent)
=> ponderation:
- (amis/enemie)
- position du lanceur (fixe ++/variable)
- target x,y  ma position inital ++
- tire enemie++ ou amie 

ajouter map de delta shot
=> danger shot enemie
=> ponderation:
- position du tireur (fixe ++/variable)
- target x,y  ma position inital ++

Classer les move fonction:
- gain score
- danger bombe/tire (wetness received)
- possible bombe/tire (wetness given) 







- best move (malus bomb zone, malus proximité allier si bombe,bonus point )


- best shot (bonus wetness + porté optimal)
- best bomb
    => verifier que la bombe ne touche pas d'allier
    => verifier la somme des wetness

combinatoire d'evaluation des bombes:
pour chaque simu => tester les zone a bombe potentiels

=> combo je block l'ancement de la bombe
Moi/En



- mettre au niveau le jet de bomb
- 
- simu sans minmax + eval
- battre le python avec des heuristiques
- push 
- mise en place du min max
- 


